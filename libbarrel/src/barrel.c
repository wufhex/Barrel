#include "barrel/barrel.h"
#include "barrel/brldef.h"
#include "barrel/brlplatform.h"
#include "brlerror_string.h"
#include <stdint.h>

static uint32_t       BRL_SizeToBin(uint64_t size);
static bool           BRL_BinPush(BRL_Archive* arch, uint64_t offset, uint64_t size);
static bool           BRL_BinPop(BRL_Archive* arch, uint64_t required_size, uint64_t* out_offset, uint64_t* out_allocated_size);
static uint32_t       BRL_HashFind(BRL_Archive* arch, uint64_t hash);
static uint32_t       BRL_HashInsertSlot(BRL_Archive* arch, uint64_t hash);
static void           BRL_FreeArchive(BRL_Archive* arch);
static void           BRL_MarkDirty(BRL_Archive* arch, uint64_t offset, uint64_t size);
static void           BRL_MarkIndexDirty(BRL_Archive* arch, uint32_t idx);

static BRL_CacheNode* BRL_LRU_Get(BRL_DataLRU* lru, uint64_t hash);
static void           BRL_LRU_Touch(BRL_DataLRU* lru, BRL_CacheNode* node);
static void           BRL_LRU_Remove(BRL_DataLRU* lru, uint64_t hash);
static void           BRL_LRU_EvictUntilFits(BRL_DataLRU* lru, uint64_t required_bytes);
bool                  BRL_LRU_Put(BRL_DataLRU* lru, uint64_t hash, const void* data, uint64_t size);

BRL_Error BRL_Create(const char* filepath, uint64_t hints, uint64_t initial_index_capacity, uint64_t max_virtual_capacity) {
    uint64_t cap = (initial_index_capacity == 0) ? BRL_DEF_INITIAL_IDX_CAPACITY_CAP : 2;
    while (cap < initial_index_capacity) {
        if (cap > UINT64_MAX / 2) return BRL_INVALID_PARAM;
        cap <<= 1;
    }

    if (cap > UINT32_MAX) return BRL_INVALID_PARAM; 

    BRL_fd fd = BRL_FOPEN_CREATE(filepath);
    if (BRL_IS_INVALID_FD(fd)) return BRL_INVALID_FD;

    BRL_DiskHeader hdr;
    BRL_MEMSET(&hdr, 0, sizeof(BRL_DiskHeader));

    uint64_t index_bytes = cap * (sizeof(uint64_t) + sizeof(BRL_EntryMeta));

    hdr.signature[0]     = BRL_SIGNATURE_0;
    hdr.signature[1]     = BRL_SIGNATURE_1;
    hdr.version          = BRL_VERSION;
    hdr.file_count       = 0;
    hdr.virtual_capacity = max_virtual_capacity;
    hdr.index_offset     = sizeof(BRL_DiskHeader);
    hdr.index_capacity   = (uint32_t)cap;
    hdr.high_water_mark  = sizeof(BRL_DiskHeader) + index_bytes;
    hdr.hints            = hints;
    hdr.variant          = BRL_VARIANT;

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

BRL_Error BRL_Open(const char* filepath, uint32_t open_flags, BRL_Archive** out_arch) {
    BRL_fd fd = BRL_FOPEN(filepath);
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

    uint64_t index_bytes = (uint64_t)temp_hdr.index_capacity * (sizeof(uint64_t) + sizeof(BRL_EntryMeta));
    if (index_bytes > UINT64_MAX - temp_hdr.index_offset || (temp_hdr.index_offset + index_bytes) > file_size) {
        BRL_FCLOSE(fd); 
        return BRL_INVALID_INDEX_BYTES;
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
    arch->index_capacity = arch->header->index_capacity;
    
    // Resolve SoA Pointers
    arch->hashes   = (uint64_t*)(mapped + arch->header->index_offset);
    arch->metadata = (BRL_EntryMeta*)(mapped + arch->header->index_offset 
        + (arch->index_capacity * sizeof(uint64_t)));

    if (arch->header->flags & BRL_ARCHIVE_PACKED) {
        BRL_Error unpack_err = BRL_Unpack(arch);
        if (unpack_err != BRL_OK) {
            BRL_FreeArchive(arch);
            return unpack_err;
        }
    }

    if (open_flags & BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE) {
        arch->compressor_lru.capacity = arch->index_capacity;
        arch->compressor_lru.hash_table = BRL_CALLOC(arch->compressor_lru.capacity, sizeof(BRL_CacheNode*));
        if (!arch->compressor_lru.hash_table) {
            BRL_FreeArchive(arch);
            return BRL_ALLOC_FAIL;
        }
        arch->compressor_lru.max_memory_bytes = BRL_COMPRESSOR_LRU_MAX_BYTES;
    }

    // Scan tombstones once to build free list bins
    for (uint32_t i = 0; i < arch->index_capacity; i++) {
        if (arch->hashes[i] == BRL_TOMBSTONE_HASH && arch->metadata[i].allocated_size > 0) {
            if (!BRL_BinPush(arch, arch->metadata[i].offset, arch->metadata[i].allocated_size)) {
                BRL_FreeArchive(arch);
                return BRL_ALLOC_FAIL;
            }
        }
    }

    arch->open_flags = open_flags;

    *out_arch = arch;
    return BRL_OK;
}

BRL_Error BRL_Close(BRL_Archive* arch) {
    if (!arch) return BRL_INVALID_PARAM;
    BRL_Error result = BRL_Sync(arch);
    BRL_FreeArchive(arch); 
    return result;
}

BRL_Error BRL_Read(BRL_Archive* arch, uint64_t hash, const uint8_t** out_data, uint64_t* out_size) {
    if (!arch) return BRL_INVALID_PARAM;

    uint32_t idx = BRL_HashFind(arch, hash);
    if (idx == UINT32_MAX) return BRL_ENTRY_NOT_FOUND;

    BRL_EntryMeta* meta = &arch->metadata[idx];
    if (meta->offset > arch->header->high_water_mark || 
        meta->compressed_size > (arch->header->high_water_mark - meta->offset)) {
        return BRL_INVALID_FILE_SIZE;
    }

    if (out_size) *out_size = meta->size;

    if (meta->flags & BRL_ENTRY_COMPRESSED) {
        if (out_data) *out_data = NULL; 
        return BRL_REQUIRES_DECOMPRESSION; 
    }

    if (out_data) *out_data = arch->mapped_data + meta->offset;
    return BRL_OK;
}

BRL_Error BRL_ReadCopy(BRL_Archive* arch, uint64_t hash, void* dst_buffer, uint64_t dst_capacity, uint64_t* out_written_size) {
    if (!arch || !dst_buffer) return BRL_INVALID_PARAM;

    // Check LRU cache 
    if (arch->open_flags & BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE) {
        BRL_CacheNode* cached = BRL_LRU_Get(&arch->compressor_lru, hash);
        if (cached) {
            if (dst_capacity < cached->size) return BRL_BUFFER_TOO_SMALL;
            BRL_MEMCPY(dst_buffer, cached->data, cached->size);
            if (out_written_size) *out_written_size = cached->size;
            return BRL_OK;
        }
    }

    uint32_t idx = BRL_HashFind(arch, hash);
    if (idx == UINT32_MAX) return BRL_ENTRY_NOT_FOUND;

    BRL_EntryMeta* meta = &arch->metadata[idx];

    if (meta->offset > arch->header->high_water_mark || meta->compressed_size > (arch->header->high_water_mark - meta->offset)) {
        return BRL_INVALID_FILE_SIZE;
    }
    if (dst_capacity < meta->size) {
        return BRL_BUFFER_TOO_SMALL;
    }

    const uint8_t* src_ptr = arch->mapped_data + meta->offset;
    if (meta->flags & BRL_ENTRY_COMPRESSED) {
        if (!arch->compressor.valid || !arch->compressor.decompress) return BRL_NO_DECOMPRESSOR;
        
        uint64_t decomp_written = arch->compressor.decompress(src_ptr, meta->compressed_size, dst_buffer, dst_capacity, hash, arch->compressor.user_data);
        if (decomp_written != meta->size) return BRL_DECOMPRESSOR_CALLBACK_FAILED;

        // Cache decompressed data
        if (arch->open_flags & BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE) {
            BRL_LRU_Put(&arch->compressor_lru, hash, dst_buffer, decomp_written);
        }

        if (out_written_size) *out_written_size = decomp_written;
        return BRL_OK;
    }

    BRL_MEMCPY(dst_buffer, src_ptr, meta->size);
    if (out_written_size) *out_written_size = meta->size;
    return BRL_OK;
}

BRL_Error BRL_WriteEx(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size, bool use_compressor) {
    if (!arch || hash <= BRL_TOMBSTONE_HASH) return BRL_INVALID_PARAM;
    if (size > 0 && !data) return BRL_INVALID_PARAM;

    const void* payload_ptr = data;
    uint64_t disk_write_size = size;
    bool is_compressed = false;

    // Use internal arena for compression
    if (size > 0 && use_compressor && arch->compressor.valid && arch->compressor.compress) {
        uint64_t max_comp = arch->compressor.get_bound ? 
            arch->compressor.get_bound(size, 
                arch->compressor.user_data) : 
            (size + (size / 8) + 256);
        
        if (arch->comp_capacity < max_comp) {
            void* new_buf = BRL_REALLOC(arch->comp_buffer, max_comp);
            if (!new_buf) return BRL_ALLOC_FAIL;
            arch->comp_buffer = new_buf;
            arch->comp_capacity = max_comp;
        }

        uint64_t comp_size = arch->compressor.compress(data, size, arch->comp_buffer, 
            arch->comp_capacity, hash, arch->compressor.user_data);
        if (comp_size > 0 && comp_size < size) {
            payload_ptr = arch->comp_buffer;
            disk_write_size = comp_size;
            is_compressed = true;
        }
    }

    uint32_t idx = BRL_HashFind(arch, hash);

    // In-place Overwrite
    if (idx != UINT32_MAX && disk_write_size <= arch->metadata[idx].allocated_size) {
        BRL_EntryMeta* meta = &arch->metadata[idx];
        if (disk_write_size > 0) {
            BRL_MEMCPY(arch->mapped_data + meta->offset, payload_ptr, disk_write_size);
            BRL_MarkDirty(arch, meta->offset, disk_write_size);
        }
        
        uint64_t slack = meta->allocated_size - disk_write_size;
        if (slack >= (1ULL << BRL_MIN_BIN_SHIFT)) {
            uint64_t slack_offset = meta->offset + disk_write_size;
            if (!BRL_BinPush(arch, slack_offset, slack)) return BRL_ALLOC_FAIL;
#ifndef BRL_DISABLE_PUNCH_HOLE
            BRL_PUNCH_HOLE(arch->fd, slack_offset, slack);
#endif
            meta->allocated_size = disk_write_size;
        }

        meta->size            = size;
        meta->compressed_size = disk_write_size;
        meta->flags           = BRL_ENTRY_ACTIVE | (is_compressed ? BRL_ENTRY_COMPRESSED : 0);
        
        BRL_MarkIndexDirty(arch, idx);
        
        if (arch->open_flags & BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE) {
            BRL_LRU_Put(&arch->compressor_lru, hash, data, size);
        }

        return BRL_OK;
    }

    // Allocate new slot & location
    uint32_t target_idx = (idx != UINT32_MAX) ? idx : BRL_HashInsertSlot(arch, hash);
    if (target_idx == UINT32_MAX) return BRL_NO_SLOT_AVAILABLE;

    uint64_t target_offset = 0;
    uint64_t allocated_size = disk_write_size;
    bool popped_from_bin = false;

    if (disk_write_size > 0) {
        popped_from_bin = BRL_BinPop(arch, disk_write_size, &target_offset, &allocated_size);
        if (popped_from_bin) {
            uint64_t slack = allocated_size - disk_write_size;
            if (slack >= (1ULL << BRL_MIN_BIN_SHIFT)) {
                uint64_t slack_offset = target_offset + disk_write_size;
                if (!BRL_BinPush(arch, slack_offset, slack)) return BRL_ALLOC_FAIL;
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
        if (!BRL_EnsureSparseAlloc(arch->fd, new_high_water_mark)) return BRL_SPARSE_ALLOC_FAIL;
    }

    uint64_t old_offset = 0;
    uint64_t old_allocated_size = 0;
    bool is_relocation = (idx != UINT32_MAX);
    if (is_relocation) {
        old_offset = arch->metadata[idx].offset;
        old_allocated_size = arch->metadata[idx].allocated_size;
    }

    if (disk_write_size > 0) {
        BRL_MEMCPY(arch->mapped_data + target_offset, payload_ptr, disk_write_size);
        BRL_MarkDirty(arch, target_offset, disk_write_size);
    }

    // Commit to slot
    arch->hashes[target_idx] = hash;
    arch->metadata[target_idx].offset          = target_offset;
    arch->metadata[target_idx].size            = size;
    arch->metadata[target_idx].compressed_size = disk_write_size;
    arch->metadata[target_idx].allocated_size  = allocated_size;
    arch->metadata[target_idx].flags           = BRL_ENTRY_ACTIVE | (is_compressed ? BRL_ENTRY_COMPRESSED : 0);
    
    if (!popped_from_bin) {
        arch->header->high_water_mark = new_high_water_mark;
        BRL_MarkDirty(arch, 0, sizeof(BRL_DiskHeader)); // Header updated
    }
    if (!is_relocation) {
        arch->header->file_count++;
        BRL_MarkDirty(arch, 0, sizeof(BRL_DiskHeader));
    }
    BRL_MarkIndexDirty(arch, target_idx);

    if (is_relocation) {
        if (!BRL_BinPush(arch, old_offset, old_allocated_size)) return BRL_ALLOC_FAIL; 
#ifndef BRL_DISABLE_PUNCH_HOLE
        if (old_allocated_size > 0) BRL_PUNCH_HOLE(arch->fd, old_offset, old_allocated_size);
#endif
    }

    if (arch->open_flags & BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE) {
        BRL_LRU_Put(&arch->compressor_lru, hash, data, size);
    }

    return BRL_OK;
}

BRL_Error BRL_Write(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size) {
    return BRL_WriteEx(arch, hash, data, size, false);
}

BRL_Error BRL_Delete(BRL_Archive* arch, uint64_t hash) {
    if (!arch) return BRL_INVALID_PARAM;
    uint32_t idx = BRL_HashFind(arch, hash);
    if (idx == UINT32_MAX) return BRL_ENTRY_NOT_FOUND;

    BRL_EntryMeta* meta = &arch->metadata[idx];

    if (!BRL_BinPush(arch, meta->offset, meta->allocated_size)) return BRL_ALLOC_FAIL;

#ifndef BRL_DISABLE_PUNCH_HOLE
    if (meta->allocated_size > 0) {
        BRL_PUNCH_HOLE(arch->fd, meta->offset, meta->allocated_size);
    }
#endif 

    arch->hashes[idx] = BRL_TOMBSTONE_HASH;
    arch->header->file_count--;

    BRL_MarkDirty(arch, 0, sizeof(BRL_DiskHeader));
    BRL_MarkIndexDirty(arch, idx);

    if (arch->open_flags & BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE) {
        BRL_LRU_Remove(&arch->compressor_lru, hash);
    }

    return BRL_OK;
}

BRL_Error BRL_Sync(BRL_Archive* arch) {
    if (!arch) return BRL_INVALID_PARAM;
    if (!arch->is_dirty) return BRL_OK;

    uint64_t page_mask = ~BRL_MSYNC_PAGE_MASK;
    uint64_t sync_start = arch->dirty_min & page_mask;
    uint64_t sync_len = (arch->dirty_max - sync_start);

    if (!BRL_MSYNC(arch->mapped_data + sync_start, sync_len)) {
        return BRL_MSYNC_FAIL;
    }

    arch->is_dirty = false;
    arch->dirty_min = 0;
    arch->dirty_max = 0;
    return BRL_OK;
}

BRL_Error BRL_Pack(BRL_Archive* arch) {
    if (!arch) return BRL_INVALID_PARAM;
    if (arch->header->flags & BRL_ARCHIVE_PACKED) return BRL_OK; // Already packed

    uint32_t count = arch->index_capacity;
    uint64_t current_offset = arch->header->index_offset + 
        ((uint64_t)count * (sizeof(uint64_t) + sizeof(BRL_EntryMeta)));

    // Shift active payloads down into a continuous block
    for (uint32_t i = 0; i < count; i++) {
        uint64_t hash = arch->hashes[i];
        if (hash >= BRL_VALID_HASH && (arch->metadata[i].flags & BRL_ENTRY_ACTIVE)) {
            BRL_EntryMeta* meta = &arch->metadata[i];

            if (meta->offset != current_offset) {
                BRL_MEMMOVE(arch->mapped_data + current_offset, 
                            arch->mapped_data + meta->offset, 
                            meta->compressed_size);
                
                meta->offset = current_offset;
                meta->allocated_size = meta->compressed_size;
                BRL_MarkIndexDirty(arch, i);
            }
            current_offset += meta->compressed_size;
        } else if (hash == BRL_TOMBSTONE_HASH) {
            // Reset tombstone metadata so stale offsets aren't indexed upon reopening
            arch->metadata[i].offset = 0;
            arch->metadata[i].allocated_size = 0;
            BRL_MarkIndexDirty(arch, i);
        }
    }

    // Preserve original capacity and transition header flag state
    arch->header->packed_virtual_capacity = arch->header->virtual_capacity;
    arch->header->virtual_capacity        = current_offset;
    arch->header->high_water_mark         = current_offset;
    arch->header->flags                  |= BRL_ARCHIVE_PACKED;

    BRL_MarkDirty(arch, 0, sizeof(BRL_DiskHeader));

    // Clear runtime free list bins since hole boundaries have been flattened
    for (uint32_t b = 0; b < BRL_NUM_BINS; b++) {
        if (arch->free_bins[b].holes) {
            BRL_FREE(arch->free_bins[b].holes);
            arch->free_bins[b].holes = NULL;
        }
        arch->free_bins[b].count = 0;
        arch->free_bins[b].capacity = 0;
    }

    BRL_Error err = BRL_Sync(arch);
    if (err != BRL_OK) return err;

    // Hard truncate on disk to free space for transfer
    if (!BRL_FTRUNCATE(arch->fd, current_offset)) {
        return BRL_ENTRY_WRITE_FAIL;
    }

    return BRL_OK;
}

BRL_Error BRL_Unpack(BRL_Archive* arch) {
    if (!arch) return BRL_INVALID_PARAM;
    if (!(arch->header->flags & BRL_ARCHIVE_PACKED)) return BRL_OK;

    uint64_t target_cap = arch->header->packed_virtual_capacity;
    if (target_cap < arch->header->high_water_mark) return BRL_INVALID_HEADER;

    // Expand physical file on disk first
    if (!BRL_FTRUNCATE(arch->fd, target_cap)) {
        return BRL_ENTRY_WRITE_FAIL;
    }

    // Expand memory mapping if current mapping size is smaller than target_cap
    if (arch->mapped_size < target_cap) {
        uint8_t* new_mapped = NULL;
        BRL_MUNMAP(arch->mapped_data, arch->mapped_size);
        
        if (!BRL_MMAP(arch->fd, target_cap, 0, (void**)&new_mapped)) {
            return BRL_MMAP_FAIL;
        }

        arch->mapped_data = new_mapped;
        arch->mapped_size = target_cap;
        arch->header      = (BRL_DiskHeader*)new_mapped;
        arch->hashes      = (uint64_t*)(new_mapped + arch->header->index_offset);
        arch->metadata    = (BRL_EntryMeta*)(new_mapped + arch->header->index_offset 
            + (arch->index_capacity * sizeof(uint64_t)));
    }

    // Reset headers and flags
    arch->header->virtual_capacity = target_cap;
    arch->header->flags &= ~BRL_ARCHIVE_PACKED;
    BRL_MarkDirty(arch, 0, sizeof(BRL_DiskHeader));

    // Punch holes across remaining unallocated regions
#ifndef BRL_DISABLE_PUNCH_HOLE
    uint32_t count = arch->index_capacity;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t hash = arch->hashes[i];
        if (hash == BRL_TOMBSTONE_HASH && arch->metadata[i].allocated_size > 0) {
            BRL_PUNCH_HOLE(arch->fd, arch->metadata[i].offset, arch->metadata[i].allocated_size);
        } else if (hash >= BRL_VALID_HASH && (arch->metadata[i].flags & BRL_ENTRY_ACTIVE)) {
            BRL_EntryMeta* meta = &arch->metadata[i];
            if (meta->allocated_size > meta->compressed_size) {
                uint64_t slack_offset = meta->offset + meta->compressed_size;
                uint64_t slack_size = meta->allocated_size - meta->compressed_size;
                BRL_PUNCH_HOLE(arch->fd, slack_offset, slack_size);
            }
        }
    }
#endif

    return BRL_Sync(arch);
}

uint64_t BRL_HashString(const char* str) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    while (*str) {
        hash ^= (uint64_t)(unsigned char)(*str++);
        hash *= 0x100000001b3ULL;
    }
    // Prevent collision with EMPTY and TOMBSTONE hashes
    if (hash <= BRL_TOMBSTONE_HASH) hash = BRL_VALID_HASH; 
    return hash;
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

BRL_Error BRL_ResizeOffline(const char* filepath, uint64_t new_capacity) {
    if (!filepath) return BRL_INVALID_PARAM;

    BRL_fd fd = BRL_FOPEN(filepath);
    if (BRL_IS_INVALID_FD(fd)) return BRL_INVALID_FD;

    BRL_DiskHeader hdr;
    uint64_t out_size = 0;
    if (!BRL_PREAD(fd, &hdr, sizeof(hdr), 0, &out_size) || out_size != sizeof(hdr)) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_HEADER;
    }
    
    // Validate magic signature & header version
    if (hdr.signature[0] != BRL_SIGNATURE_0 || hdr.signature[1] != BRL_SIGNATURE_1) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_MAGIC;
    }
    if (hdr.version != BRL_VERSION) {
        BRL_FCLOSE(fd);
        return BRL_INVALID_VERSION;
    }

    // Capacity must be able to hold the header and index structures
    uint64_t index_bytes = (uint64_t)hdr.index_capacity * (sizeof(uint64_t) + sizeof(BRL_EntryMeta));
    uint64_t min_required = hdr.index_offset + index_bytes;
    
    if (new_capacity < min_required) {
        BRL_FCLOSE(fd);
        return BRL_RESIZE_SIZE_TOO_SMALL;
    }

    // Capacity cannot truncate existing written entries below hwm
    if (new_capacity < hdr.high_water_mark) {
        BRL_FCLOSE(fd);
        return BRL_RESIZE_DATA_TRUNCATION;
    }

    // Update physical file boundary on disk
    if (!BRL_FTRUNCATE(fd, new_capacity)) {
        BRL_FCLOSE(fd);
        return BRL_ENTRY_WRITE_FAIL;
    }

    // Commit updated virtual_capacity to header
    hdr.virtual_capacity = new_capacity;
    if (!BRL_PWRITE(fd, &hdr, sizeof(hdr), 0, &out_size) || out_size != sizeof(hdr)) {
        BRL_FCLOSE(fd);
        return BRL_HEADER_WRITE_FAIL;
    }

    BRL_FCLOSE(fd);
    return BRL_OK;
}

static uint32_t BRL_SizeToBin(uint64_t size) {
    if (size <= (1ULL << BRL_MIN_BIN_SHIFT)) return 0;
    uint32_t bin = 64 - BRL_CLZLL(size - 1) - BRL_MIN_BIN_SHIFT;
    return (bin >= BRL_NUM_BINS) ? (BRL_NUM_BINS - 1) : bin;
}

static bool BRL_BinPush(BRL_Archive* arch, uint64_t offset, uint64_t size) {
    uint32_t bin_idx = BRL_SizeToBin(size);
    BRL_FreeBin* bin = &arch->free_bins[bin_idx];
    
    // realloc only runs when the array runs out of room (at 0, 16, 32, 64, 128... items)
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

static bool BRL_BinPop(BRL_Archive* arch, uint64_t required_size, uint64_t* out_offset, uint64_t* out_allocated_size) {
    uint32_t start_bin = BRL_SizeToBin(required_size);
    
    for (uint32_t i = start_bin; i < BRL_NUM_BINS; i++) {
        BRL_FreeBin* bin = &arch->free_bins[i];
        if (bin->count == 0) continue;

        // If we are checking a larger bin (i > start_bin), 
        // ANY hole in this bin is guaranteed to be >= required_size.
        // We can skip the scan and pop the last element instantly in true O(1).
        if (i > start_bin) {
            uint32_t last_idx = bin->count - 1;
            *out_offset = bin->holes[last_idx].offset;
            *out_allocated_size = bin->holes[last_idx].size;
            bin->count--;
            return true;
        }

        // For the exact starting bin, we must verify the size fits.
        for (uint32_t j = 0; j < bin->count; j++) {
            if (bin->holes[j].size >= required_size) {
                *out_offset = bin->holes[j].offset;
                *out_allocated_size = bin->holes[j].size;
                
                // Move the last element into the deleted slot
                bin->holes[j] = bin->holes[bin->count - 1];
                bin->count--;
                return true;
            }
        }
    }
    
    return false;
}

// Cache Locality SoA Probing
static uint32_t BRL_HashFind(BRL_Archive* arch, uint64_t hash) {
    if (hash <= BRL_TOMBSTONE_HASH) return UINT32_MAX;
    uint32_t mask = arch->index_capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);

    for (uint32_t i = 0; i < arch->index_capacity; i++) {
        uint64_t h = arch->hashes[idx];
        if (h == hash) return idx;
        if (h == BRL_EMPTY_HASH) return UINT32_MAX;
        idx = (idx + 1) & mask;
    }
    return UINT32_MAX;
}

static uint32_t BRL_HashInsertSlot(BRL_Archive* arch, uint64_t hash) {
    if (hash <= BRL_TOMBSTONE_HASH) return UINT32_MAX;
    uint32_t mask = arch->index_capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);
    uint32_t first_tombstone = UINT32_MAX;

    for (uint32_t i = 0; i < arch->index_capacity; i++) {
        uint64_t h = arch->hashes[idx];
        if (h == hash) return idx;
        if (h == BRL_TOMBSTONE_HASH && first_tombstone == UINT32_MAX) {
            first_tombstone = idx;
        }
        if (h == BRL_EMPTY_HASH) {
            return (first_tombstone != UINT32_MAX) ? first_tombstone : idx;
        }
        idx = (idx + 1) & mask;
    }
    return first_tombstone; 
}

static void BRL_MarkDirty(BRL_Archive* arch, uint64_t offset, uint64_t size) {
    if (size == 0) return;
    if (!arch->is_dirty) {
        arch->dirty_min = offset;
        arch->dirty_max = offset + size;
        arch->is_dirty = true;
    } else {
        if (offset < arch->dirty_min) arch->dirty_min = offset;
        if (offset + size > arch->dirty_max) arch->dirty_max = offset + size;
    }
}

static void BRL_MarkIndexDirty(BRL_Archive* arch, uint32_t idx) {
    BRL_MarkDirty(arch, arch->header->index_offset + (idx * sizeof(uint64_t)), sizeof(uint64_t));
    BRL_MarkDirty(arch, arch->header->index_offset + (arch->index_capacity * sizeof(uint64_t)) + (idx * sizeof(BRL_EntryMeta)), sizeof(BRL_EntryMeta));
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
        if (arch->free_bins[i].holes) BRL_FREE(arch->free_bins[i].holes);
    }

    if (arch->comp_buffer) BRL_FREE(arch->comp_buffer);

    BRL_FREE(arch);
}

static BRL_CacheNode* BRL_LRU_Get(BRL_DataLRU* lru, uint64_t hash) {
    if (!lru || !lru->hash_table || lru->capacity == 0) return NULL;
    uint32_t mask = lru->capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);

    for (uint32_t i = 0; i < lru->capacity; i++) {
        BRL_CacheNode* node = lru->hash_table[idx];
        if (!node) return NULL;
        if (node->hash == hash) {
            BRL_LRU_Touch(lru, node);
            return node;
        }
        idx = (idx + 1) & mask;
    }
    return NULL;
}

static void BRL_LRU_Touch(BRL_DataLRU* lru, BRL_CacheNode* node) {
    if (lru->head == node) return;

    // Unlink
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (lru->tail == node) lru->tail = node->prev;

    // Relink to head
    node->next = lru->head;
    node->prev = NULL;
    if (lru->head) lru->head->prev = node;
    lru->head = node;
    if (!lru->tail) lru->tail = node;
}

static void BRL_LRU_Remove(BRL_DataLRU* lru, uint64_t hash) {
    if (!lru || !lru->hash_table || lru->capacity == 0) return;
    uint32_t mask = lru->capacity - 1;
    uint32_t i = (uint32_t)(hash & mask);

    for (uint32_t probe = 0; probe < lru->capacity; probe++) {
        if (lru->hash_table[i] == NULL) return;
        if (lru->hash_table[i]->hash == hash) {
            BRL_CacheNode* node = lru->hash_table[i];

            // Unlink from list
            if (node->prev) node->prev->next = node->next;
            if (node->next) node->next->prev = node->prev;
            if (lru->head == node) lru->head = node->next;
            if (lru->tail == node) lru->tail = node->prev;

            lru->current_bytes -= node->size;
            BRL_FREE(node->data);
            BRL_FREE(node);

            // Backward shift deletion to maintain open-address probing invariant
            lru->hash_table[i] = NULL;
            uint32_t j = i;
            while (1) {
                j = (j + 1) & mask;
                if (lru->hash_table[j] == NULL) break;
                uint32_t k = (uint32_t)(lru->hash_table[j]->hash & mask);
                if ((i <= j) ? (i < k && k <= j) : (i < k || k <= j)) continue;
                lru->hash_table[i] = lru->hash_table[j];
                lru->hash_table[j] = NULL;
                i = j;
            }
            return;
        }
        i = (i + 1) & mask;
    }
}

static void BRL_LRU_EvictUntilFits(BRL_DataLRU* lru, uint64_t required_bytes) {
    while (lru->tail && (lru->current_bytes + required_bytes > lru->max_memory_bytes)) {
        BRL_LRU_Remove(lru, lru->tail->hash);
    }
}

bool BRL_LRU_Put(BRL_DataLRU* lru, uint64_t hash, const void* data, uint64_t size) {
    if (!lru || !lru->hash_table || lru->max_memory_bytes == 0 || size == 0) return false;
    
    // Ignore items larger than 25% of total capacity limit
    if (size > (lru->max_memory_bytes / 4)) return false;

    // Remove old entry if it already exists
    BRL_LRU_Remove(lru, hash);

    // Evict space if needed
    BRL_LRU_EvictUntilFits(lru, size);

    BRL_CacheNode* node = BRL_CALLOC(1, sizeof(BRL_CacheNode));
    if (!node) return false;

    node->data = BRL_MALLOC(size);
    if (!node->data) {
        BRL_FREE(node);
        return false;
    }

    BRL_MEMCPY(node->data, data, size);
    node->size = size;
    node->hash = hash;

    // Open Addressing linear probing insertion
    uint32_t mask = lru->capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);
    while (lru->hash_table[idx] != NULL) {
        idx = (idx + 1) & mask;
    }
    lru->hash_table[idx] = node;

    // Prepend to MRU
    node->next = lru->head;
    if (lru->head) lru->head->prev = node;
    lru->head = node;
    if (!lru->tail) lru->tail = node;

    lru->current_bytes += size;
    return true;
}
