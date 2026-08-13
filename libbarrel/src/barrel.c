#include "barrel/barrel.h"
#include "barrel/brldef.h"
#include "barrel/brlplatform.h"
#include <stdint.h>

static uint32_t BRL_SizeToBin(uint64_t size);
static bool BRL_BinPush(BRL_Archive* arch, uint64_t offset, uint64_t size);
static bool BRL_BinPop(BRL_Archive* arch, uint64_t required_size, 
    uint64_t* out_offset, uint64_t* out_allocated_size);
static BRL_DiskEntry* BRL_HashFind(BRL_Archive* arch, uint64_t hash);
static BRL_DiskEntry* BRL_HashInsertSlot(BRL_Archive* arch, uint64_t hash);
static void BRL_FreeArchive(BRL_Archive* arch);

static const char* const __g_BRL_ErrorString[] = {
    "The operation completed successfully.",           // BRL_OK
    "An invalid parameter was provided.",               // BRL_INVALID_PARAM

    "An invalid file descriptor was provided.",         // BRL_INVALID_FD
    "Invalid file size.",                               // BRL_INVALID_FILE_SIZE
    "General read failure.",                            // BRL_READ_FAIL
    "General write failure.",                           // BRL_WRITE_FAIL

    "Wrong header.",                                    // BRL_INVALID_HEADER
    "Wrong magic: cur[0] != 'A' OR cur[1] != 'E'.",     // BRL_INVALID_MAGIC
    "The archive version is invalid or incompatible.",  // BRL_INVALID_VERSION
    "Index capacity in header failed safety checks.",   // BRL_INVALID_IDX_CAPACITY
    "Index offset in header failed safety checks.",     // BRL_INVALID_IDX_OFFSET
    "Water mark in header is less than index offset.",  // BRL_HI_WATER_MARK_LESS_THAN_IDX_OFFSET
    "Water mark in header is more than file size.",     // BRL_HI_WATER_MARK_MORE_THAN_FILE_SIZE
    "Water mark in header is more than virtual capacity.", // BRL_HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY
    "The calculated index bytes are larger than the file size.", // BRL_INVALID_INDEX_BYTES
    "Virtual capacity exceeds the maximum allowed by Barrel.", // BRL_VIRTUAL_CAPACITY_MORE_THAN_MAX

    "Failure to write header.",                         // BRL_HEADER_WRITE_FAIL
    "Failure to read an entry.",                        // BRL_ENTRY_READ_FAIL
    "Failure to write an entry.",                       // BRL_ENTRY_WRITE_FAIL
    "Sparse allocation failed.",                        // BRL_SPARSE_ALLOC_FAIL

    "Allocation failed.",                               // BRL_ALLOC_FAIL
    "Memory mapping failed.",                           // BRL_MMAP_FAIL
    "Memory sync failed.",                              // BRL_MSYNC_FAIL

    "The requested entry was not found.",               // BRL_ENTRY_NOT_FOUND
    "No more slots available.",                         // BRL_NO_SLOT_AVAILABLE

    "Barrel encountered a compressed entry, but no compressor was set.", // BRL_NO_DECOMPRESSOR
    "Barrel tried to compress/decompress an entry, but the callback returned an error.", // BRL_DECOMPRESSOR_CALLBACK_FAILED
    "Entry is compressed on disk; use BRL_ReadCopy.",   // BRL_REQUIRES_DECOMPRESSION
    "The provided destination buffer is too small.",     // BRL_BUFFER_TOO_SMALL
    "Unknown error. I don't even know how you got this error :P" // BRL_UNKNOWN 
};

BRL_Error BRL_Create(const char* filepath, uint64_t hints, uint64_t initial_index_capacity, uint64_t max_virtual_capacity) {
    uint64_t cap = BRL_DEF_INITIAL_IDX_CAPACITY_CAP; 
    while (cap < initial_index_capacity) {
        if (cap > UINT64_MAX / 2) return BRL_INVALID_PARAM;
        cap <<= 1;
    }

    if (cap > UINT32_MAX) return BRL_INVALID_PARAM; 

    int fd = BRL_FOPEN_CREATE(filepath);
    if (BRL_IS_INVALID_FD(fd)) return BRL_INVALID_FD;

    BRL_DiskHeader hdr;
    BRL_MEMSET(&hdr, 0, sizeof(BRL_DiskHeader));

    hdr.signature[0]     = BRL_SIGNATURE_0;
    hdr.signature[1]     = BRL_SIGNATURE_1;
    hdr.version          = BRL_VERSION;
    hdr.file_count       = 0;
    hdr.virtual_capacity = max_virtual_capacity;
    hdr.index_offset     = sizeof(BRL_DiskHeader);
    hdr.index_capacity   = (uint32_t)cap;
    hdr.high_water_mark  = sizeof(BRL_DiskHeader) + (cap * sizeof(BRL_DiskEntry));
    hdr.hints            = hints;

    uint64_t out_size = 0;
    if (!BRL_FWRITE(fd, &hdr, sizeof(hdr), &out_size) || out_size != sizeof(hdr)) {
        BRL_FCLOSE(fd); 
        return BRL_HEADER_WRITE_FAIL; 
    }

    if (!BRL_FTRUNCATE(fd, hdr.high_water_mark)) {
        BRL_FCLOSE(fd);
        return BRL_ENTRY_WRITE_FAIL;
    }

    BRL_FCLOSE(fd);
    return BRL_OK;
}

BRL_Error BRL_Open(const char* filepath, BRL_Archive** out_arch) {
    int fd = BRL_FOPEN(filepath);
    if (BRL_IS_INVALID_FD(fd)) return BRL_INVALID_FD;

    uint64_t file_size = 0;
    if (!BRL_FSIZE(fd, &file_size) || file_size < sizeof(BRL_DiskHeader)) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_FILE_SIZE;
    }

    BRL_DiskHeader temp_hdr;
    uint64_t out_size = 0;
    if (!BRL_PREAD(fd, &temp_hdr, sizeof(temp_hdr), 0, &out_size) || out_size != sizeof(temp_hdr)) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_HEADER;
    }

    if (temp_hdr.signature[0] != BRL_SIGNATURE_0 || temp_hdr.signature[1] != BRL_SIGNATURE_1) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_MAGIC;
    }
    if (temp_hdr.version != BRL_VERSION) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_VERSION;
    }
    if (temp_hdr.index_capacity == 0 || (temp_hdr.index_capacity & (temp_hdr.index_capacity - 1)) != 0) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_IDX_CAPACITY;
    }
    if (temp_hdr.index_offset < sizeof(BRL_DiskHeader)) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_IDX_OFFSET;
    }
    if (temp_hdr.high_water_mark > file_size) {
        BRL_FCLOSE(fd);
        return BRL_HI_WATER_MARK_MORE_THAN_FILE_SIZE;
    }

    // Index Math Verification
    uint64_t index_bytes = (uint64_t)temp_hdr.index_capacity * sizeof(BRL_DiskEntry);
    
    // Protect addition from overflow
    if (index_bytes > UINT64_MAX - temp_hdr.index_offset) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_INDEX_BYTES;
    }
    
    uint64_t index_end = temp_hdr.index_offset + index_bytes;

    if (index_end > file_size) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_INDEX_BYTES;
    }
    if (temp_hdr.high_water_mark < index_end) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_HEADER;
    }
    if (temp_hdr.high_water_mark > temp_hdr.virtual_capacity) {
        BRL_FCLOSE(fd);
        return BRL_HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY;
    }

    uint8_t* mapped = NULL;
    if (!BRL_MMAP(fd, temp_hdr.virtual_capacity, 0, (void**)&mapped)) {
        BRL_FCLOSE(fd);
        return BRL_MMAP_FAIL;
    }

    BRL_Archive* arch = BRL_CALLOC(1, sizeof(BRL_Archive));
    if (!arch) {
        BRL_MUNMAP(mapped, temp_hdr.virtual_capacity);
        BRL_FCLOSE(fd);
        return BRL_ALLOC_FAIL;
    }

    arch->fd             = fd;
    arch->mapped_data    = mapped;
    arch->mapped_size    = temp_hdr.virtual_capacity;
    arch->header         = (BRL_DiskHeader*)mapped;
    arch->index          = (BRL_DiskEntry*)(mapped + arch->header->index_offset);
    arch->index_capacity = arch->header->index_capacity;

    // Scan index once to build free list bins
    for (uint32_t i = 0; i < arch->index_capacity; i++) {
        BRL_DiskEntry* entry = &arch->index[i];
        if (entry->flags == BRL_ENTRY_FREE && entry->allocated_size > 0) {
            if (!BRL_BinPush(arch, entry->offset, entry->allocated_size)) {
                BRL_FreeArchive(arch);
                return BRL_ALLOC_FAIL;
            }
        }
    }

    *out_arch = arch;
    return BRL_OK;
}

BRL_Error BRL_Close(BRL_Archive* arch) {
    BRL_Error result = BRL_OK;
    if (!arch) return BRL_INVALID_PARAM;

    if (!BRL_MSYNC(arch->mapped_data, arch->header->high_water_mark)) {
        result = BRL_MSYNC_FAIL;
    }

    BRL_FreeArchive(arch); 
    return result;
}

BRL_Error BRL_Read(BRL_Archive* arch, uint64_t hash, const uint8_t** out_data, uint64_t* out_size) {
    if (!arch) return BRL_INVALID_PARAM;

    BRL_DiskEntry* entry = BRL_HashFind(arch, hash);
    if (!entry) return BRL_ENTRY_NOT_FOUND;

    // bounds verify against high water mark 
    if (entry->offset > arch->header->high_water_mark || 
        entry->compressed_size > (arch->header->high_water_mark - entry->offset)) {
        return BRL_INVALID_FILE_SIZE;
    }

    if (out_size) *out_size = entry->size;

    // If compressed, notify that decompression is required via BRL_ReadCopy()
    if (entry->flags & BRL_ENTRY_COMPRESSED) {
        if (out_data) *out_data = NULL; 
        return BRL_REQUIRES_DECOMPRESSION; 
    }

    if (out_data) *out_data = arch->mapped_data + entry->offset;
    return BRL_OK;
}

BRL_Error BRL_ReadCopy(BRL_Archive* arch, uint64_t hash, 
    void* dst_buffer, uint64_t dst_capacity, uint64_t* out_written_size) {
    if (!arch || !dst_buffer) return BRL_INVALID_PARAM;

    BRL_DiskEntry* entry = BRL_HashFind(arch, hash);
    if (!entry) return BRL_ENTRY_NOT_FOUND;

    // bounds verify against high water mark 
    if (entry->offset > arch->header->high_water_mark || 
        entry->compressed_size > (arch->header->high_water_mark - entry->offset)) {
        return BRL_INVALID_FILE_SIZE;
    }

    if (dst_capacity < entry->size) {
        return BRL_BUFFER_TOO_SMALL;
    }

    const uint8_t* src_ptr = arch->mapped_data + entry->offset;

    // Handle Compressed Entry
    if (entry->flags & BRL_ENTRY_COMPRESSED) {
        if (!arch->compressor.valid || !arch->compressor.decompress) {
            return BRL_NO_DECOMPRESSOR;
        }

        uint64_t decomp_written = arch->compressor.decompress(
            src_ptr,
            entry->compressed_size,
            dst_buffer,
            dst_capacity,
            arch->compressor.user_data
        );

        if (decomp_written != entry->size) {
            return BRL_DECOMPRESSOR_CALLBACK_FAILED;
        }

        if (out_written_size) *out_written_size = decomp_written;
        return BRL_OK;
    }

    // Handle Uncompressed Entry
    BRL_MEMCPY(dst_buffer, src_ptr, entry->size);
    if (out_written_size) *out_written_size = entry->size;
    return BRL_OK;
}

BRL_Error BRL_WriteEx(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size, bool use_compressor) {
    if (!arch || hash == BRL_EMPTY_HASH) return BRL_INVALID_PARAM;
    if (size > 0 && !data) return BRL_INVALID_PARAM;

    const void* payload_ptr = data;
    uint64_t disk_write_size = size;
    bool is_compressed = false;
    void* comp_buffer = NULL;

    // Attempt Compression if required, compressor registered and flag enabled
    if (size > 0 && use_compressor && arch->compressor.valid && arch->compressor.compress) {
        uint64_t max_comp_bound = size;
        if (arch->compressor.get_bound) {
            max_comp_bound = arch->compressor.get_bound(size, 
                arch->compressor.user_data);
        } else {
            // netural fallback, LZ4 like
            max_comp_bound = size + (size / 8) + 256;
        }
        comp_buffer = BRL_MALLOC(max_comp_bound);

        if (comp_buffer) {
            uint64_t comp_size = arch->compressor.compress(
                data, size, comp_buffer, max_comp_bound, arch->compressor.user_data
            );

            if (comp_size > 0 && comp_size < size) {
                payload_ptr = comp_buffer;
                disk_write_size = comp_size;
                is_compressed = true;
            }
        }
    }

    BRL_DiskEntry* entry = BRL_HashFind(arch, hash);

    // In-place Overwrite
    if (entry && disk_write_size <= entry->allocated_size) {
        if (disk_write_size > 0) {
            BRL_MEMCPY(arch->mapped_data + entry->offset, payload_ptr, disk_write_size);
        }
        
        uint64_t slack = entry->allocated_size - disk_write_size;
        if (slack >= (1ULL << BRL_MIN_BIN_SHIFT)) {
            uint64_t slack_offset = entry->offset + disk_write_size;
            if (!BRL_BinPush(arch, slack_offset, slack)) {
                if (comp_buffer) BRL_FREE(comp_buffer);
                return BRL_ALLOC_FAIL;
            }

#ifndef BRL_DISABLE_PUNCH_HOLE
            BRL_PUNCH_HOLE(arch->fd, slack_offset, slack);
#endif
            entry->allocated_size = disk_write_size;
        }

        entry->size            = size;
        entry->compressed_size = disk_write_size;
        entry->flags           = BRL_ENTRY_ACTIVE | (is_compressed ? BRL_ENTRY_COMPRESSED : 0);

        if (comp_buffer) BRL_FREE(comp_buffer);
        return BRL_OK;
    }

    // Hash Slot Lookup & New Allocation
    BRL_DiskEntry* slot = BRL_HashInsertSlot(arch, hash);
    if (!slot) {
        if (comp_buffer) BRL_FREE(comp_buffer);
        return BRL_NO_SLOT_AVAILABLE;
    }

    uint64_t target_offset = 0;
    uint64_t allocated_size = disk_write_size;
    bool popped_from_bin = false;

    // Zero-byte allocations don't query bins or move HWM
    if (disk_write_size > 0) {
        popped_from_bin = BRL_BinPop(arch, disk_write_size, 
            &target_offset, &allocated_size);

        if (popped_from_bin) {
            // Split excess hole space (slack) back to free bins
            uint64_t slack = allocated_size - disk_write_size;
            if (slack >= (1ULL << BRL_MIN_BIN_SHIFT)) {
                uint64_t slack_offset = target_offset + disk_write_size;
                if (!BRL_BinPush(arch, slack_offset, slack)) {
                    if (comp_buffer) BRL_FREE(comp_buffer);
                    return BRL_ALLOC_FAIL;
                }
#ifndef BRL_DISABLE_PUNCH_HOLE
                BRL_PUNCH_HOLE(arch->fd, slack_offset, slack);
#endif
                allocated_size = disk_write_size;
            }
        }
    }

    uint64_t new_high_water_mark = arch->header->high_water_mark;
    if (!popped_from_bin) {
        target_offset = arch->header->high_water_mark;
        new_high_water_mark += disk_write_size;
        if (!BRL_EnsureSparseAlloc(arch->fd, new_high_water_mark)) {
            if (comp_buffer) BRL_FREE(comp_buffer);
            return BRL_SPARSE_ALLOC_FAIL;
        }
    }

    uint64_t old_offset = 0;
    uint64_t old_allocated_size = 0;
    bool is_relocation = (entry != NULL);
    if (is_relocation) {
        old_offset = entry->offset;
        old_allocated_size = entry->allocated_size;
    }

    if (disk_write_size > 0) {
        BRL_MEMCPY(arch->mapped_data + target_offset, payload_ptr, disk_write_size);
    }

    // Populate Slot
    slot->hash            = hash;
    slot->offset          = target_offset;
    slot->size            = size;
    slot->compressed_size = disk_write_size;
    slot->allocated_size  = allocated_size;
    slot->flags           = BRL_ENTRY_ACTIVE | (is_compressed ? BRL_ENTRY_COMPRESSED : 0);
    
    if (!popped_from_bin) arch->header->high_water_mark = new_high_water_mark;
    if (!is_relocation) arch->header->file_count++;

    if (is_relocation) {
        if (!BRL_BinPush(arch, old_offset, old_allocated_size)) {
            if (comp_buffer) BRL_FREE(comp_buffer);
            return BRL_ALLOC_FAIL; 
        }

#ifndef BRL_DISABLE_PUNCH_HOLE
        if (old_allocated_size > 0) {
            BRL_PUNCH_HOLE(arch->fd, old_offset, old_allocated_size);
        }
#endif
    }

    if (comp_buffer) BRL_FREE(comp_buffer);
    return BRL_OK;
}

BRL_Error BRL_Write(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size) {
    return BRL_WriteEx(arch, hash, data, size, false);
}

BRL_Error BRL_Delete(BRL_Archive* arch, uint64_t hash) {
    if (!arch) return BRL_INVALID_PARAM;
    BRL_DiskEntry* entry = BRL_HashFind(arch, hash);
    if (!entry) return BRL_ENTRY_NOT_FOUND;

    if (!BRL_BinPush(arch, entry->offset, entry->allocated_size)) {
        return BRL_ALLOC_FAIL;
    }

#ifndef BRL_DISABLE_PUNCH_HOLE
    // Reclaim physical storage for the freed entry
    if (entry->allocated_size > 0) {
        BRL_PUNCH_HOLE(arch->fd, entry->offset, entry->allocated_size);
    }
#endif // BRL_DISABLE_PUNCH_HOLE

    entry->flags = BRL_ENTRY_TOMBSTONE;
    arch->header->file_count--;
    return BRL_OK;
}

BRL_Error BRL_Sync(BRL_Archive* arch) {
    if (!arch) return BRL_INVALID_PARAM;

    if (!BRL_MSYNC(arch->mapped_data, arch->header->high_water_mark)) {
        return BRL_MSYNC_FAIL;
    }

    return BRL_OK;
}

uint64_t BRL_HashString(const char* str) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    while (*str) {
        hash ^= (uint64_t)(unsigned char)(*str++);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

BRL_Compressor* BRL_GetDecompressor(BRL_Archive* arch) {
    if (!arch || !arch->compressor.valid) return NULL; 
    return &arch->compressor;
}

bool BRL_SetCompressor(BRL_Archive* arch, const BRL_Compressor* compressor) {
    if (!arch) return false;
    
    if (!compressor) {
        BRL_MEMSET(&arch->compressor, 0, sizeof(BRL_Compressor));
        return false;
    }

    arch->compressor = *compressor;
    arch->compressor.valid = true;
    return true;
}

const char* BRL_FormatError(BRL_Error err) {
    if (err < 0 || err > BRL_UNKNOWN) {
        return __g_BRL_ErrorString[BRL_UNKNOWN];
    }
    return __g_BRL_ErrorString[err];
}

// Private

static uint32_t BRL_SizeToBin(uint64_t size) {
    if (size <= (1ULL << BRL_MIN_BIN_SHIFT)) return 0;
    uint32_t bin = 64 - BRL_CLZLL(size - 1) - BRL_MIN_BIN_SHIFT;
    return (bin >= BRL_NUM_BINS) ? (BRL_NUM_BINS - 1) : bin;
}

static bool BRL_BinPush(BRL_Archive* arch, uint64_t offset, uint64_t size) {
    uint32_t bin_idx = BRL_SizeToBin(size);
    BRL_FreeBin* bin = &arch->free_bins[bin_idx];
    
    if (bin->count >= bin->capacity) {
        uint32_t new_cap = (bin->capacity == 0) ? 16 : bin->capacity * 2;
        
        BRL_OrphanHole* new_holes = BRL_REALLOC(bin->holes, new_cap * sizeof(BRL_OrphanHole));
        if (!new_holes) return false; 

        bin->holes = new_holes;
        bin->capacity = new_cap;
    }
    bin->holes[bin->count].offset = offset;
    bin->holes[bin->count].size = size;
    bin->count++;

    return true;
}

static bool BRL_BinPop(BRL_Archive* arch, uint64_t required_size, 
    uint64_t* out_offset, uint64_t* out_allocated_size) {
    uint32_t start_bin = BRL_SizeToBin(required_size);
    for (uint32_t i = start_bin; i < BRL_NUM_BINS; i++) {
        BRL_FreeBin* bin = &arch->free_bins[i];
        for (int32_t j = (int32_t)bin->count - 1; j >= 0; j--) {
            if (bin->holes[j].size >= required_size) {
                *out_offset = bin->holes[j].offset;
                *out_allocated_size = bin->holes[j].size;
                
                // Remove hole from bin (swap with last element)
                bin->holes[j] = bin->holes[bin->count - 1];
                bin->count--;
                return true;
            }
        }
    }
    return false;
}

static BRL_DiskEntry* BRL_HashFind(BRL_Archive* arch, uint64_t hash) {
    if (hash == BRL_EMPTY_HASH) return NULL;
    uint32_t mask = arch->index_capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);

    for (uint32_t i = 0; i < arch->index_capacity; i++) {
        BRL_DiskEntry* entry = &arch->index[idx];
        if (entry->hash == hash && (entry->flags & BRL_ENTRY_ACTIVE)) {
            return entry;
        }
        // Stop probing only when hitting an untouched FREE slot
        if (entry->flags == BRL_ENTRY_FREE && entry->hash == BRL_EMPTY_HASH) {
            return NULL; 
        }
        idx = (idx + 1) & mask;
    }
    return NULL;
}

static BRL_DiskEntry* BRL_HashInsertSlot(BRL_Archive* arch, uint64_t hash) {
    uint32_t mask = arch->index_capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);
    BRL_DiskEntry* first_tombstone = NULL;

    for (uint32_t i = 0; i < arch->index_capacity; i++) {
        BRL_DiskEntry* entry = &arch->index[idx];

        if ((entry->flags & BRL_ENTRY_ACTIVE) && entry->hash == hash) {
            return entry;
        }

        if (entry->flags == BRL_ENTRY_TOMBSTONE && !first_tombstone) {
            first_tombstone = entry;
        }

        if (entry->flags == BRL_ENTRY_FREE && entry->hash == BRL_EMPTY_HASH) {
            return first_tombstone ? first_tombstone : entry;
        }

        idx = (idx + 1) & mask;
    }
    return first_tombstone;
}

static void BRL_FreeArchive(BRL_Archive* arch) {
    if (!arch) return;
    
    if (arch->mapped_data) {
        BRL_MUNMAP(arch->mapped_data, arch->mapped_size);
    }
    
    if (!BRL_IS_INVALID_FD(arch->fd)) {
        BRL_FCLOSE(arch->fd);
    }

    for (uint32_t i = 0; i < BRL_NUM_BINS; i++) {
        if (arch->free_bins[i].holes) {
            BRL_FREE(arch->free_bins[i].holes);
        }
    }

    BRL_FREE(arch);
}
