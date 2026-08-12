#include "barrel/barrel.h"

#include <stdio.h>
#include <string.h>
#include <limits>
#include <vector>

#include <lz4.h>

constexpr int INT_MAX = std::numeric_limits<int>::max();

typedef struct DummyData {
    int    id;
    float  position[3];
    double timestamp;
} DummyData;

uint64_t Test_CompressFunc(
        const void* src,
        uint64_t    src_size,
        void*       dst,
        uint64_t    dst_capacity,
        void*       user_data
) {
    (void)user_data;

    if (src_size > INT_MAX || dst_capacity > INT_MAX) {
        return 0;
    }

    int compressed_size = LZ4_compress_default(
        (const char*)src,
        (char*)dst,
        (int)src_size,
        (int)dst_capacity
    );

    if (compressed_size <= 0) {
        return 0;
    }

    return (uint64_t)compressed_size;
}

uint64_t Test_DecompressFunc(
        const void* src,
        uint64_t    src_size,
        void*       dst,
        uint64_t    dst_capacity,
        void*       user_data
) {
    (void)user_data;

    if (src_size > INT_MAX || dst_capacity > INT_MAX) {
        return 0;
    }

    int decompressed_size = LZ4_decompress_safe(
        (const char*)src,
        (char*)dst,
        (int)src_size,
        (int)dst_capacity
    );

    if (decompressed_size < 0) {
        return 0;
    }

    return (uint64_t)decompressed_size;
}

uint64_t Test_GetBoundFunc(uint64_t src_size, void* user_data) {
    (void)user_data;
    if (src_size > INT_MAX) return 0;
    return (uint64_t)LZ4_compressBound((int)src_size);
}

BRL_Compressor g_compressor = {
    true,
    Test_CompressFunc,
    Test_DecompressFunc,
    Test_GetBoundFunc,
    nullptr
};

int main() {
    BRL_Error err = BRL_OK;

    // Hints is a 8 characters long static string useful to hint a program on how to handle a Barrel archive.
    // In this case, it's not needed, but this string can hint another program
    // that entries are compressed using LZ4. 
    constexpr const char* Hints = "LZ4     ";
    err = BRL_Create("./example.brl", Hints, BRL_DEF_INITIAL_IDX_CAPACITY_CAP);
    if (err != BRL_OK) {
        printf("Error creating archive: %d\n", (int)err);
        return 1;
    }

    BRL_Archive* arch = NULL;

    err = BRL_Open("./example.brl", &arch);
    if (err != BRL_OK) {
        printf("Error opening archive: %d\n", (int)err);
        return 1;
    }

    printf("Read hint: \"%s\"\n\n", arch->header->hints);

    if (!BRL_SetCompressor(arch, &g_compressor)) {
        printf("Error setting compressor.\n");
        BRL_Close(arch);
        return 1;
    }

    // Write Text Data

    const char* text_data = "Hello World!";
    uint64_t text_len = strlen(text_data) + 1;

    uint64_t text_hash = BRL_HASH_CONST("DATA_TEXT");

    // Do not compress this entry.
    err = BRL_Write(
        arch,
        text_hash,
        text_data,
        text_len
    );

    if (err != BRL_OK) {
        printf("Error writing text data: %d\n", (int)err);
        BRL_Close(arch);
        return 1;
    }

    // Write Binary Data

    DummyData bin_data = {
        .id = 42,
        .position = { 10.5f, -3.2f, 100.0f },
        .timestamp = 1718920000.5
    };

    uint64_t bin_hash = BRL_HASH_CONST("DATA_BINARY");

    // Do not compress this entry.
    err = BRL_Write(
        arch,
        bin_hash,
        &bin_data,
        sizeof(bin_data)
    );

    if (err != BRL_OK) {
        printf("Error writing binary data: %d\n", (int)err);
        BRL_Close(arch);
        return 1;
    }

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

    // Request compression for massive data entry
    err = BRL_WriteEx(
        arch,
        massive_hash,
        massive_input.data(),
        massive_size,
        true
    );

    if (err != BRL_OK) {
        printf("Error writing massive data: %d\n", (int)err);
        BRL_Close(arch);
        return 1;
    }

    // Read Text Data

    const uint8_t* read_text_ptr = NULL;
    uint64_t read_text_size = 0;

    err = BRL_Read(
        arch,
        text_hash,
        &read_text_ptr,
        &read_text_size
    );

    if (err != BRL_OK) {
        printf("Error reading text data: %d\n", (int)err);
        BRL_Close(arch);
        return 1;
    }

    printf("--- Read Text File ---\n");
    printf("Size: %llu bytes\n",
           (unsigned long long)read_text_size);
    printf("Content: \"%s\"\n\n",
           (const char*)read_text_ptr);

    // Read Binary Data

    const uint8_t* read_bin_ptr = NULL;
    uint64_t read_bin_size = 0;

    err = BRL_Read(
        arch,
        bin_hash,
        &read_bin_ptr,
        &read_bin_size
    );

    if (err != BRL_OK) {
        printf("Error reading binary data: %d\n", (int)err);
        BRL_Close(arch);
        return 1;
    }

    const DummyData* player = (const DummyData*)read_bin_ptr;

    printf("--- Read Binary File ---\n");
    printf("Entry was stored uncompressed.\n");
    printf("Size: %llu bytes\n",
           (unsigned long long)read_bin_size);
    printf("Player ID: %d\n", player->id);
    printf("Position : [%.1f, %.1f, %.1f]\n",
           player->position[0],
           player->position[1],
           player->position[2]);
    printf("Timestamp: %.1f\n\n",
           player->timestamp);

    // Read Massive Data

    const uint8_t* read_massive_ptr = NULL;
    uint64_t read_massive_size = 0;

    err = BRL_Read(
        arch,
        massive_hash,
        &read_massive_ptr,
        &read_massive_size
    );

    if (err == BRL_REQUIRES_DECOMPRESSION) {
        // BRL_Read() cannot return a zero-copy pointer for compressed data.
        //
        // read_massive_size is already the ORIGINAL/uncompressed size, so use it
        // to allocate the destination buffer.

        uint8_t* decompressed_data =
            new uint8_t[read_massive_size];

        uint64_t decompressed_size = 0;
        err = BRL_ReadCopy(
            arch,
            massive_hash,
            decompressed_data,
            read_massive_size,
            &decompressed_size
        );

        if (err != BRL_OK) {
            printf("Error decompressing data: %d\n", (int)err);
            delete[] decompressed_data;
            BRL_Close(arch);
            return 1;
        }

        bool matches = (memcmp(massive_input.data(), decompressed_data, massive_size) == 0);

        printf("--- Read Massive File ---\n");
        printf("Compressed entry required decompression.\n");
        printf("Decompressed Size: %llu bytes\n",
               (unsigned long long)decompressed_size);
        printf("Integrity Check  : %s\n\n",
               matches ? "OK" : "FAILED");

        delete[] decompressed_data;
    } else if (err == BRL_OK) {
        printf("--- Read Massive File ---\n");
        printf("Entry was stored uncompressed.\n");
        printf("Size: %llu bytes\n\n",
               (unsigned long long)read_massive_size);
    }
    else {
        printf("Error reading massive data: %d\n", (int)err);
        BRL_Close(arch);
        return 1;
    }

    // Close

    err = BRL_Close(arch);
    if (err != BRL_OK) {
        printf("Error closing archive: %d\n", (int)err);
        return 1;
    }

    printf("All Done!\n");
    printf("Just a quick format error test: %s\n", BRL_FormatError(err));
    return 0;
}