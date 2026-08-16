#include "barrel/barrel.h"
#include "barrel/brldef.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <cstring>
#include <cassert>
#include <fstream>
#include <chrono>
#include <limits>
#include <filesystem>
#include <algorithm>
#include <lz4.h>

constexpr uint32_t OpenFlagNormal            = BRL_OPEN_NORMAL;
constexpr uint32_t OpenFlagWithCompressorLRU = BRL_OPEN_NORMAL | BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << " [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(test_func) \
    do { \
        std::cout << "[RUNNING] " << #test_func << "... " << std::flush; \
        if (test_func()) { \
            std::cout << "\033[32m[PASSED]\033[0m\n"; \
        } else { \
            std::cout << "\033[31m[FAILED]\033[0m\n"; \
            failed_tests++; \
        } \
        total_tests++; \
    } while(0)

constexpr int BRL_INT_MAX = std::numeric_limits<int>::max();

static uint64_t LZ4_CompressWrapper(const void* src, uint64_t src_sz, void* dst, uint64_t dst_cap, uint64_t, void*) {
    if (src_sz > BRL_INT_MAX || dst_cap > BRL_INT_MAX) return 0;
    int res = LZ4_compress_default((const char*)src, (char*)dst, (int)src_sz, (int)dst_cap);
    return res <= 0 ? 0 : (uint64_t)res;
}

int g_DecompressionAmount = 0;

static uint64_t LZ4_DecompressWrapper(const void* src, uint64_t src_sz, void* dst, uint64_t dst_cap, uint64_t, void*) {
    g_DecompressionAmount++;
    if (src_sz > BRL_INT_MAX || dst_cap > BRL_INT_MAX) return 0;
    int res = LZ4_decompress_safe((const char*)src, (char*)dst, (int)src_sz, (int)dst_cap);
    return res < 0 ? 0 : (uint64_t)res;
}

static uint64_t LZ4_GetBoundWrapper(uint64_t src_sz, void*) {
    if (src_sz > BRL_INT_MAX) return 0;
    return (uint64_t)LZ4_compressBound((int)src_sz);
}

static BRL_Compressor g_test_compressor = {
    true, LZ4_CompressWrapper, LZ4_DecompressWrapper, LZ4_GetBoundWrapper, nullptr
};

static std::vector<uint8_t> GenerateRandomBuffer(size_t size, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> buf(size);
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>(rng() & 0xFF);
    }
    return buf;
}

bool Test_EdgeCases() {
    const char* path = "./test_edge.brl";
    BRL_Create(path, 0, 256, 16 * 1024 * 1024);

    BRL_Archive* arch = nullptr;
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_OK, "Failed to open archive.");

    // A. Reserved Hashes (0 = EMPTY, 1 = TOMBSTONE)
    uint8_t dummy[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT(BRL_Write(arch, 0, dummy, 8) == BRL_INVALID_PARAM, "BRL_Write must reject EMPTY_HASH (0)");
    TEST_ASSERT(BRL_Write(arch, 1, dummy, 8) == BRL_INVALID_PARAM, "BRL_Write must reject TOMBSTONE_HASH (1)");

    // B. Zero-byte payloads
    uint64_t zero_hash = BRL_HashString("ZERO_BYTE_ENTRY");
    TEST_ASSERT(BRL_Write(arch, zero_hash, nullptr, 0) == BRL_OK, "Failed to write 0-byte entry");
    
    const uint8_t* read_ptr = nullptr;
    uint64_t read_sz = 999;
    TEST_ASSERT(BRL_Read(arch, zero_hash, &read_ptr, &read_sz) == BRL_OK, "Failed to read 0-byte entry");
    TEST_ASSERT(read_sz == 0, "0-byte read size mismatch");

    // C. Non-existent Hash Lookup
    TEST_ASSERT(BRL_Read(arch, 0xDEADBEEF, &read_ptr, &read_sz) == BRL_ENTRY_NOT_FOUND, "Found non-existent entry");
    TEST_ASSERT(BRL_Delete(arch, 0xDEADBEEF) == BRL_ENTRY_NOT_FOUND, "Deleted non-existent entry");

    // D. In-place Overwrite (Shrinking & Growing)
    uint64_t overwrite_hash = BRL_HashString("OVERWRITE_TEST");
    std::vector<uint8_t> buf_large = GenerateRandomBuffer(1024, 1);
    std::vector<uint8_t> buf_small = GenerateRandomBuffer(256, 2);

    TEST_ASSERT(BRL_Write(arch, overwrite_hash, buf_large.data(), buf_large.size()) == BRL_OK, "Write large failed");
    TEST_ASSERT(BRL_Write(arch, overwrite_hash, buf_small.data(), buf_small.size()) == BRL_OK, "In-place shrink failed");
    
    TEST_ASSERT(BRL_Read(arch, overwrite_hash, &read_ptr, &read_sz) == BRL_OK, "Read after shrink failed");
    TEST_ASSERT(read_sz == 256, "Size mismatch after shrink");
    TEST_ASSERT(memcmp(read_ptr, buf_small.data(), 256) == 0, "Data corruption after shrink");

    BRL_Close(arch);
    return true;
}

bool Test_SlotExhaustion() {
    const char* path = "./test_capacity.brl";
    // Force initial capacity cap to 16
    BRL_Create(path, 0, 16, 1024 * 1024);

    BRL_Archive* arch = nullptr;
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_OK, "Failed to open archive.");

    uint8_t val = 0xAB;
    // Fill all 16 hash slots
    for (uint32_t i = 0; i < 16; i++) {
        uint64_t hash = 100 + i;
        TEST_ASSERT(BRL_Write(arch, hash, &val, 1) == BRL_OK, "Failed to write during capacity fill");
    }

    // 17th write must fail with BRL_NO_SLOT_AVAILABLE
    TEST_ASSERT(BRL_Write(arch, 9999, &val, 1) == BRL_NO_SLOT_AVAILABLE, "Exhausted hash table did not return BRL_NO_SLOT_AVAILABLE");

    // Delete one entry to create a tombstone
    TEST_ASSERT(BRL_Delete(arch, 100) == BRL_OK, "Failed to delete slot");

    // 17th write should now succeed by taking the tombstone slot
    TEST_ASSERT(BRL_Write(arch, 9999, &val, 1) == BRL_OK, "Failed to reuse tombstone slot");

    BRL_Close(arch);
    return true;
}

bool Test_FragmentationAndRecycling() {
    const char* path = "./test_frag.brl";
    BRL_Create(path, 0, 1024, 64 * 1024 * 1024);

    BRL_Archive* arch = nullptr;
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_OK, "Failed to open archive.");

    constexpr size_t ENTRY_SIZE = 4096; // 4KB
    constexpr size_t NUM_ENTRIES = 100;
    std::vector<uint8_t> payload = GenerateRandomBuffer(ENTRY_SIZE, 42);

    // Write 100 contiguous entries
    for (size_t i = 0; i < NUM_ENTRIES; ++i) {
        BRL_Write(arch, 1000 + i, payload.data(), payload.size());
    }

    uint64_t hwm_after_initial_writes = arch->header->high_water_mark;

    // Delete every even entry (creates 50 interleaved holes)
    for (size_t i = 0; i < NUM_ENTRIES; i += 2) {
        TEST_ASSERT(BRL_Delete(arch, 1000 + i) == BRL_OK, "Delete failed during fragmentation setup");
    }

    // Write 50 NEW entries of identical size
    for (size_t i = 0; i < 50; ++i) {
        TEST_ASSERT(BRL_Write(arch, 5000 + i, payload.data(), payload.size()) == BRL_OK, "Write failed during recycling");
    }

    // High water mark should NOT have expanded, as all 50 entries fit into recycled holes!
    TEST_ASSERT(arch->header->high_water_mark == hwm_after_initial_writes, 
                "High Water Mark grew! Bin recycling failed to reuse orphan holes.");

    BRL_Close(arch);
    return true;
}

bool Test_FuzzingAndCorruption() {
    const char* path = "./test_fuzz.brl";
    BRL_Create(path, 0, 256, 16 * 1024 * 1024);

    // Fill archive with valid data first
    BRL_Archive* arch = nullptr;
    BRL_Open(path, OpenFlagNormal, &arch);
    std::vector<uint8_t> buf = GenerateRandomBuffer(512, 99);
    for (int i = 0; i < 10; ++i) BRL_Write(arch, 500 + i, buf.data(), buf.size());
    BRL_Close(arch);

    // A. Truncated File
    {
        std::filesystem::resize_file(path, 10); // Physically truncates file to 10 bytes
    }
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_INVALID_FILE_SIZE, "Opened truncated file without error");
   
    // Re-create valid file
    BRL_Create(path, 0, 256, 16 * 1024 * 1024);

    // B. Corrupt Magic Signature
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(0);
        f.write("XX", 2); // Overwrite 'AE'
    }
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_INVALID_MAGIC, "Opened archive with corrupted magic bytes");

    // C. Corrupt Index Capacity (Non-Power of 2)
    {
        BRL_Create(path, 0, 256, 16 * 1024 * 1024);
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        // Index capacity sits after magic(2), version(2), flags(4), file_count(8), virtual_cap(8), hwm(8), index_offset(8)
        // Offset = 2+2+4+8+8+8+8 = 40
        uint32_t invalid_cap = 300; // Not a power of two
        f.seekp(40);
        f.write(reinterpret_cast<const char*>(&invalid_cap), sizeof(invalid_cap));
    }
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_INVALID_IDX_CAPACITY, "Opened archive with invalid index capacity");

    return true;
}

bool Test_DifferentialStress() {
    const char* path = "./test_stress.brl";
    constexpr uint64_t VIRTUAL_CAP = 128 * 1024 * 1024; // 128MiB
    BRL_Create(path, 0, 2048, VIRTUAL_CAP);

    BRL_Archive* arch = nullptr;
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_OK, "Failed to open stress test archive");
    BRL_SetCompressor(arch, &g_test_compressor);

    std::unordered_map<uint64_t, std::vector<uint8_t>> ground_truth;
    std::vector<uint64_t> active_keys;

    std::mt19937 rng(1337);
    constexpr int NUM_OPERATIONS = 10000;

    for (int op = 0; op < NUM_OPERATIONS; ++op) {
        int action = rng() % 100;

        if (action < 50 || active_keys.empty()) { 
            // 50% Write / Overwrite
            uint64_t hash = (rng() % 500) + 2; // Keep working set within range [2, 501]
            size_t size = (rng() % 8192) + 1;  // Random payload 1B - 8KB
            bool use_comp = (rng() % 2) == 0;

            std::vector<uint8_t> data = GenerateRandomBuffer(size, rng());

            BRL_Error res = BRL_WriteEx(arch, hash, data.data(), size, use_comp);
            
            if (res == BRL_OK) {
                if (ground_truth.find(hash) == ground_truth.end()) {
                    active_keys.push_back(hash);
                }
                ground_truth[hash] = data;
            } else {
                TEST_ASSERT(res == BRL_NO_SLOT_AVAILABLE || res == BRL_SPARSE_ALLOC_FAIL, "Unexpected write error");
            }
        } 
        else if (action < 80) { 
            // 30% Read & Verify Integrity
            size_t idx = rng() % active_keys.size();
            uint64_t hash = active_keys[idx];

            const std::vector<uint8_t>& expected = ground_truth[hash];

            const uint8_t* ptr = nullptr;
            uint64_t read_sz = 0;
            BRL_Error read_err = BRL_Read(arch, hash, &ptr, &read_sz);

            if (read_err == BRL_REQUIRES_DECOMPRESSION) {
                std::vector<uint8_t> decomp_buf(expected.size());
                uint64_t decomp_written = 0;
                
                BRL_Error copy_err = BRL_ReadCopy(arch, hash, decomp_buf.data(), decomp_buf.size(), &decomp_written);
                TEST_ASSERT(copy_err == BRL_OK, "ReadCopy failed on compressed entry");
                TEST_ASSERT(decomp_written == expected.size(), "Decompressed size mismatch");
                TEST_ASSERT(memcmp(decomp_buf.data(), expected.data(), expected.size()) == 0, "Corrupted data on decompress");
            } else {
                TEST_ASSERT(read_err == BRL_OK, "Read failed on uncompressed entry");
                TEST_ASSERT(read_sz == expected.size(), "Uncompressed size mismatch");
                TEST_ASSERT(memcmp(ptr, expected.data(), expected.size()) == 0, "Corrupted data on read");
            }
        } 
        else { 
            // 20% Delete
            size_t idx = rng() % active_keys.size();
            uint64_t hash = active_keys[idx];

            TEST_ASSERT(BRL_Delete(arch, hash) == BRL_OK, "Delete failed in stress test");

            ground_truth.erase(hash);
            active_keys[idx] = active_keys.back();
            active_keys.pop_back();
        }
    }

    // Final Sync and Verification Test
    TEST_ASSERT(BRL_Sync(arch) == BRL_OK, "BRL_Sync failed");
    BRL_Close(arch);

    // Reopen and check if all ground truth data persisted correctly across closing/opening
    TEST_ASSERT(BRL_Open(path, OpenFlagNormal, &arch) == BRL_OK, "Failed to reopen archive after stress test");
    BRL_SetCompressor(arch, &g_test_compressor);

    for (const auto& [hash, expected] : ground_truth) {
        std::vector<uint8_t> actual(expected.size());
        uint64_t written = 0;
        BRL_Error err = BRL_ReadCopy(arch, hash, actual.data(), actual.size(), &written);
        TEST_ASSERT(err == BRL_OK, "Failed ReadCopy on post-stress reload");
        TEST_ASSERT(memcmp(actual.data(), expected.data(), expected.size()) == 0, "Data mismatch on post-stress reload");
    }

    BRL_Close(arch);
    return true;
}

bool Test_Benchmark() {
    using Clock = std::chrono::high_resolution_clock;

    auto measure = [](const char* label, auto&& func) {
        auto t_start = Clock::now();
        auto result = func();
        auto t_end = Clock::now();

        double us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
        printf("%-40s | %9.2f us | %8.3f ms\n", label, us, us / 1000.0);
        return result;
    };

    auto start_total = Clock::now();
    constexpr uint64_t Capacity = 16 * 1024 * 1024; // 16MiB

    printf("\n\n");

    // BRL_Create
    BRL_Error err = measure("BRL_Create (16MiB Virtual Cap)", [&]() {
        return BRL_Create("./example.brl", 0ULL, BRL_DEF_INITIAL_IDX_CAPACITY_CAP, Capacity);
    });
    if (err != BRL_OK) return false;

    // BRL_Open
    BRL_Archive* arch = nullptr;
    err = measure("BRL_Open", [&]() {
        return BRL_Open("./example.brl", OpenFlagNormal, &arch);
    });
    if (err != BRL_OK || !arch) return false;

    // BRL_SetCompressor
    bool comp_ok = measure("BRL_SetCompressor", [&]() {
        return BRL_SetCompressor(arch, &g_test_compressor);
    });
    if (!comp_ok) {
        BRL_Close(arch);
        return false;
    }

    // Write Text Data
    const char* text_data = "Hello World!";
    uint64_t text_len = strlen(text_data) + 1;
    uint64_t text_hash = BRL_HASH_CONST("DATA_TEXT");

    measure("BRL_Write (Small Text, 13 B)", [&]() {
        return BRL_Write(arch, text_hash, text_data, text_len);
    });

    // Write Massive Data (Compressed)
    constexpr uint64_t massive_size = 10 * 1024 * 1024; // 10 MiB
    const char pattern[] = "Barrel Archive Format with LZ4 Compression! ";
    size_t pattern_len = strlen(pattern);

    std::vector<char> massive_input(massive_size);
    for (size_t i = 0; i < massive_size - 1; ++i) {
        massive_input[i] = pattern[i % pattern_len];
    }
    massive_input[massive_size - 1] = '\0';
    uint64_t massive_hash = BRL_HASH_CONST("DATA_MASSIVE");

    measure("BRL_WriteEx (Massive, 10 MiB LZ4)", [&]() {
        return BRL_WriteEx(arch, massive_hash, massive_input.data(), massive_size, true);
    });

    // Read Text Data
    const uint8_t* read_text_ptr = nullptr;
    uint64_t read_text_size = 0;

    measure("BRL_Read (Small Text Zero-Copy)", [&]() {
        return BRL_Read(arch, text_hash, &read_text_ptr, &read_text_size);
    });

    // Read Massive Data Meta
    const uint8_t* read_massive_ptr = nullptr;
    uint64_t read_massive_size = 0;

    err = measure("BRL_Read (Massive Meta Check)", [&]() {
        return BRL_Read(arch, massive_hash, &read_massive_ptr, &read_massive_size);
    });

    // ReadCopy / Decompress Massive Data
    if (err == BRL_REQUIRES_DECOMPRESSION) {
        uint8_t* decompressed_data = new uint8_t[read_massive_size];
        uint64_t decompressed_size = 0;

        measure("BRL_ReadCopy (Decompress 10 MiB)", [&]() {
            return BRL_ReadCopy(arch, massive_hash, decompressed_data, read_massive_size, &decompressed_size);
        });

        delete[] decompressed_data;
    }

    // Close
    measure("BRL_Close (Includes Sync & Unmap)", [&]() {
        return BRL_Close(arch);
    });

    auto end_total = Clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    printf("-----------------------------------------------------------------\n");
    printf("%-40s | %9.2f ms\n", "TOTAL BENCHMARK TIME", total_ms);
    printf("-----------------------------------------------------------------\n");

    return true;
}

bool Test_CompressorDecompressorLRU() {
    using Clock = std::chrono::high_resolution_clock;

    const char* path = "./test_compressor_lru.brl";

    constexpr uint64_t VIRTUAL_CAP = 512ULL * 1024 * 1024;
    constexpr size_t ENTRY_COUNT = 5;
    constexpr size_t ENTRY_SIZE = 10ULL * 1024 * 1024; // 10 MiB each
    constexpr int ROUNDS = 20;

    std::cout << "\n";
    std::cout << "  Compressor + Decompressor + LRU stress:\n";
    std::cout << "    Entries : " << ENTRY_COUNT << "\n";
    std::cout << "    Size    : " << ENTRY_SIZE / (1024 * 1024) << " MiB each\n";
    std::cout << "    Total   : "
              << (ENTRY_COUNT * ENTRY_SIZE) / (1024 * 1024) << " MiB\n";
    std::cout << "    Rounds  : " << ROUNDS << "\n";

    std::error_code ec;
    std::filesystem::remove(path, ec);

    g_DecompressionAmount = 0;

    TEST_ASSERT(
        BRL_Create(path, 0, 1024, VIRTUAL_CAP) == BRL_OK,
        "Failed to create compressor/LRU test archive"
    );

    BRL_Archive* arch = nullptr;

    TEST_ASSERT(
        BRL_Open(path, OpenFlagWithCompressorLRU, &arch) == BRL_OK,
        "Failed to open compressor/LRU test archive"
    );

    TEST_ASSERT(
        BRL_SetCompressor(arch, &g_test_compressor),
        "Failed to install LZ4 compressor"
    );

    struct TestEntry {
        uint64_t hash;
        std::vector<uint8_t> data;
    };

    std::vector<TestEntry> entries;
    entries.reserve(ENTRY_COUNT);

    // Generate several different highly-compressible 10 MiB payloads.
    // Each entry is different, so the test cannot accidentally pass because
    // all entries contain the same data.

    for (size_t i = 0; i < ENTRY_COUNT; ++i) {
        TestEntry entry;

        char name[64];
        std::snprintf(name, sizeof(name), "LRU_50MB_ENTRY_%zu", i);

        entry.hash = BRL_HashString(name);
        entry.data.resize(ENTRY_SIZE);

        // Different deterministic patterns for each entry.
        // Still highly compressible by LZ4.
        const uint32_t seed = static_cast<uint32_t>(0x12340000u + i * 0x1111u);

        for (size_t j = 0; j < ENTRY_SIZE; ++j) {
            uint32_t value =
                seed ^
                static_cast<uint32_t>((j / 4096) * 2654435761u);

            entry.data[j] = static_cast<uint8_t>(
                (value >> ((j / 1024) & 3) * 8) & 0xFF
            );
        }

        entries.push_back(std::move(entry));
    }

    // Write all entries compressed.
    //
    // Total logical data = 50 MiB.

    auto write_start = Clock::now();

    for (size_t i = 0; i < entries.size(); ++i) {
        BRL_Error err = BRL_WriteEx(
            arch,
            entries[i].hash,
            entries[i].data.data(),
            entries[i].data.size(),
            true
        );

        TEST_ASSERT(
            err == BRL_OK,
            "Compressed write failed"
        );

        const uint32_t idx = [&]() -> uint32_t {
            uint32_t mask = arch->index_capacity - 1;
            uint32_t probe = static_cast<uint32_t>(entries[i].hash & mask);

            for (uint32_t n = 0; n < arch->index_capacity; ++n) {
                if (arch->hashes[probe] == entries[i].hash)
                    return probe;

                probe = (probe + 1) & mask;
            }

            return UINT32_MAX;
        }();

        TEST_ASSERT(idx != UINT32_MAX, "Written entry disappeared from index");

        TEST_ASSERT(
            (arch->metadata[idx].flags & BRL_ENTRY_COMPRESSED) != 0,
            "Entry was not stored compressed"
        );

        TEST_ASSERT(
            arch->metadata[idx].compressed_size < arch->metadata[idx].size,
            "LZ4 failed to actually compress test data"
        );
    }

    auto write_end = Clock::now();

    const double write_ms =
        std::chrono::duration<double, std::milli>(
            write_end - write_start
        ).count();

    std::cout
        << "    Initial compressed write: "
        << write_ms << " ms\n";

    // First pass:
    //
    // Every entry must actually decompress correctly.
    //
    // This populates the decompressed-data LRU.

    std::vector<uint8_t> output(ENTRY_SIZE);

    for (size_t i = 0; i < entries.size(); ++i) {
        uint64_t written = 0;

        BRL_Error err = BRL_ReadCopy(
            arch,
            entries[i].hash,
            output.data(),
            output.size(),
            &written
        );

        TEST_ASSERT(
            err == BRL_OK,
            "Initial decompression failed"
        );

        TEST_ASSERT(
            written == entries[i].data.size(),
            "Initial decompression size mismatch"
        );

        TEST_ASSERT(
            std::memcmp(
                output.data(),
                entries[i].data.data(),
                entries[i].data.size()
            ) == 0,
            "Initial decompression data mismatch"
        );
    }

    // Verify the LRU actually contains data.
    //
    // current_bytes is intentionally inspected because this is a test
    // compiled against the internal BRL_Archive definition.

    TEST_ASSERT(
        arch->compressor_lru.current_bytes > 0,
        "Decompressed-data LRU was not populated"
    );

    std::cout
        << "    LRU after first pass: "
        << arch->compressor_lru.current_bytes / (1024 * 1024)
        << " MiB\n";

    // Repeated mixed-access workload.
    //
    // Each round accesses all entries in a different order. This exercises:
    //
    //   - decompression
    //   - LRU insertion
    //   - LRU lookup
    //   - LRU touch / MRU promotion
    //   - eviction
    //   - repeated decompression after eviction
    //
    // We intentionally don't access entries in the same order every time.

    std::mt19937 rng(0xBADC0DE);

    uint64_t total_reads = 0;
    uint64_t total_bytes = 0;

    auto stress_start = Clock::now();

    for (int round = 0; round < ROUNDS; ++round) {
        std::vector<size_t> order(ENTRY_COUNT);

        for (size_t i = 0; i < ENTRY_COUNT; ++i)
            order[i] = i;

        std::shuffle(order.begin(), order.end(), rng);

        for (size_t access = 0; access < order.size(); ++access) {
            const size_t entry_idx = order[access];
            const TestEntry& entry = entries[entry_idx];

            uint64_t written = 0;

            BRL_Error err = BRL_ReadCopy(
                arch,
                entry.hash,
                output.data(),
                output.size(),
                &written
            );

            TEST_ASSERT(
                err == BRL_OK,
                "Mixed LRU ReadCopy failed"
            );

            TEST_ASSERT(
                written == entry.data.size(),
                "Mixed LRU decompressed size mismatch"
            );

            TEST_ASSERT(
                std::memcmp(
                    output.data(),
                    entry.data.data(),
                    entry.data.size()
                ) == 0,
                "Mixed LRU decompressed data mismatch"
            );

            total_reads++;
            total_bytes += written;
        }

        std::cout
            << "    Round " << (round + 1)
            << "/" << ROUNDS
            << " OK | LRU: "
            << arch->compressor_lru.current_bytes / (1024 * 1024)
            << " MiB\n";
    }

    auto stress_end = Clock::now();

    const double stress_ms =
        std::chrono::duration<double, std::milli>(
            stress_end - stress_start
        ).count();

    TEST_ASSERT(
        total_reads == ENTRY_COUNT * ROUNDS,
        "Unexpected number of LRU reads"
    );

    TEST_ASSERT(
        total_bytes == ENTRY_COUNT * ROUNDS * ENTRY_SIZE,
        "Unexpected number of decompressed bytes"
    );

    std::cout
        << "    Stress reads : " << total_reads << "\n"
        << "    Data checked : "
        << total_bytes / (1024 * 1024)
        << " MiB\n"
        << "    Stress time  : "
        << stress_ms << " ms\n";

    // Now test the important cache-coherency case:
    //
    // overwrite an existing compressed entry, then read it repeatedly.
    // The old decompressed cache entry must not survive the overwrite.

    const size_t overwrite_idx = 0;

    std::vector<uint8_t> replacement(ENTRY_SIZE);

    for (size_t i = 0; i < replacement.size(); ++i) {
        replacement[i] = static_cast<uint8_t>(
            ((i * 31) ^ (i >> 7) ^ 0xA5) & 0xFF
        );
    }

    BRL_Error overwrite_err = BRL_WriteEx(
        arch,
        entries[overwrite_idx].hash,
        replacement.data(),
        replacement.size(),
        true
    );

    TEST_ASSERT(
        overwrite_err == BRL_OK,
        "Compressed overwrite failed"
    );

    // Update our expected data.
    entries[overwrite_idx].data = replacement;

    // Read it multiple times. The first read may decompress and populate
    // the cache; subsequent reads must return the replacement data rather
    // than stale data from before the overwrite.
    for (int i = 0; i < 5; ++i) {
        uint64_t written = 0;

        BRL_Error err = BRL_ReadCopy(
            arch,
            entries[overwrite_idx].hash,
            output.data(),
            output.size(),
            &written
        );

        TEST_ASSERT(
            err == BRL_OK,
            "Read after compressed overwrite failed"
        );

        TEST_ASSERT(
            written == replacement.size(),
            "Read after overwrite returned wrong size"
        );

        TEST_ASSERT(
            std::memcmp(
                output.data(),
                replacement.data(),
                replacement.size()
            ) == 0,
            "LRU returned stale data after compressed overwrite"
        );
    }

    // Delete an entry and verify its cache entry is invalidated.

    const uint64_t deleted_hash = entries[1].hash;

    TEST_ASSERT(
        BRL_Delete(arch, deleted_hash) == BRL_OK,
        "Failed to delete LRU test entry"
    );

    uint64_t deleted_written = 0;

    BRL_Error deleted_read = BRL_ReadCopy(
        arch,
        deleted_hash,
        output.data(),
        output.size(),
        &deleted_written
    );

    TEST_ASSERT(
        deleted_read == BRL_ENTRY_NOT_FOUND,
        "Deleted entry was still returned from LRU"
    );

    // Sync, close, reopen, and verify again.
    // The LRU itself is intentionally ephemeral. After reopening, all
    // compressed entries should still decompress correctly from disk.

    TEST_ASSERT(
        BRL_Sync(arch) == BRL_OK,
        "BRL_Sync failed after compressor/LRU stress"
    );

    TEST_ASSERT(
        BRL_Close(arch) == BRL_OK,
        "BRL_Close failed after compressor/LRU stress"
    );

    arch = nullptr;

    TEST_ASSERT(
        BRL_Open(path, OpenFlagWithCompressorLRU, &arch) == BRL_OK,
        "Failed to reopen compressor/LRU test archive"
    );

    TEST_ASSERT(
        BRL_SetCompressor(arch, &g_test_compressor),
        "Failed to restore compressor after reopen"
    );

    // Entry 1 was deleted; every other entry must survive.
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i == 1)
            continue;

        uint64_t written = 0;

        BRL_Error err = BRL_ReadCopy(
            arch,
            entries[i].hash,
            output.data(),
            output.size(),
            &written
        );

        TEST_ASSERT(
            err == BRL_OK,
            "Post-reopen decompression failed"
        );

        TEST_ASSERT(
            written == entries[i].data.size(),
            "Post-reopen decompressed size mismatch"
        );

        TEST_ASSERT(
            std::memcmp(
                output.data(),
                entries[i].data.data(),
                entries[i].data.size()
            ) == 0,
            "Post-reopen decompressed data mismatch"
        );
    }

    // Final benchmark: repeat reads after reopening.
    // This measures the decompression/LRU path on a clean archive.

    constexpr int FINAL_ROUNDS = 10;
    auto final_start = Clock::now();

    for (int round = 0; round < FINAL_ROUNDS; ++round) {
        std::vector<size_t> order;

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i != 1)
                order.push_back(i);
        }

        std::shuffle(order.begin(), order.end(), rng);

        for (size_t entry_idx : order) {
            uint64_t written = 0;

            BRL_Error err = BRL_ReadCopy(
                arch,
                entries[entry_idx].hash,
                output.data(),
                output.size(),
                &written
            );

            TEST_ASSERT(
                err == BRL_OK,
                "Final LRU benchmark read failed"
            );

            TEST_ASSERT(
                written == entries[entry_idx].data.size(),
                "Final LRU benchmark size mismatch"
            );

            TEST_ASSERT(
                std::memcmp(
                    output.data(),
                    entries[entry_idx].data.data(),
                    entries[entry_idx].data.size()
                ) == 0,
                "Final LRU benchmark data mismatch"
            );
        }
    }

    auto final_end = Clock::now();

    const double final_ms =
        std::chrono::duration<double, std::milli>(
            final_end - final_start
        ).count();

    const uint64_t final_bytes =
        static_cast<uint64_t>(ENTRY_COUNT - 1) *
        ENTRY_SIZE *
        FINAL_ROUNDS;

    const double throughput =
        (final_bytes / (1024.0 * 1024.0)) /
        (final_ms / 1000.0);

    std::cout
        << "    Final benchmark : "
        << final_bytes / (1024 * 1024)
        << " MiB verified in "
        << final_ms
        << " ms ("
        << throughput
        << " MiB/s)\n";

    std::cout << "    Decompression callback called " << g_DecompressionAmount << " times." << std::endl;

    TEST_ASSERT(
        arch->compressor_lru.current_bytes > 0,
        "LRU was not populated after reopen"
    );

    TEST_ASSERT(
        BRL_Close(arch) == BRL_OK,
        "Final BRL_Close failed"
    );

    std::filesystem::remove(path, ec);

    return true;
}

int main() {
    int total_tests = 0;
    int failed_tests = 0;

    RUN_TEST(Test_EdgeCases);
    RUN_TEST(Test_SlotExhaustion);
    RUN_TEST(Test_FragmentationAndRecycling);
    RUN_TEST(Test_FuzzingAndCorruption);
    RUN_TEST(Test_DifferentialStress);
    RUN_TEST(Test_Benchmark);
    RUN_TEST(Test_CompressorDecompressorLRU);

    std::cout << "\n---------------------------------------------------------\n";
    if (failed_tests == 0) {
        std::cout << "\033[32mALL " << total_tests << " TEST SUITES PASSED SUCCESSFULLY!\033[0m\n";
    } else {
        std::cout << "\033[31m" << failed_tests << " OUT OF " << total_tests << " TEST SUITES FAILED.\033[0m\n";
    }
    std::cout << "---------------------------------------------------------\n";

    return failed_tests == 0 ? 0 : 1;
}
