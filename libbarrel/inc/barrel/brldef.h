#pragma once

#include "brlcompfix.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef BRL_DISABLE_STATIC_LEN_CHECKS 
#include <stddef.h>
#endif // BRL_DISABLE_STATIC_LEN_CHECKS

#include "brlapi.h"
#include "brlplatform.h"

BRL_EXTERN_C_START

// Barrel Signature
#define BRL_SIGNATURE_0       'A'
#define BRL_SIGNATURE_1       'E'

#define BRL_VERSION           0x0100

#define BRL_VARIANT           0x59424F4F4F4F4353

#ifndef BRL_NUM_BINS
#define BRL_NUM_BINS          16
#endif // BRL_NUM_BINS

#ifndef BRL_MIN_BIN_SHIFT
#define BRL_MIN_BIN_SHIFT     8
#endif // BRL_MIN_BIN_SHIFT

// Magic hashes for SoA logic
#define BRL_EMPTY_HASH        0ULL
#define BRL_TOMBSTONE_HASH    1ULL
#define BRL_VALID_HASH        (BRL_TOMBSTONE_HASH + 1ULL)

#ifndef BRL_DEF_INITIAL_IDX_CAPACITY_CAP 
#define BRL_DEF_INITIAL_IDX_CAPACITY_CAP 256ULL
#endif // BRL_DEF_INITIAL_IDX_CAPACITY_CAP

#ifndef BRL_MSYNC_PAGE_MASK
#define BRL_MSYNC_PAGE_MASK 4095ULL
#endif // BRL_MSYNC_PAGE_MASK

#ifndef BRL_COMPRESSOR_LRU_MAX_BYTES 
#define BRL_COMPRESSOR_LRU_MAX_BYTES 64 * 1024 * 1024ULL
#endif // BRL_COMPRESSOR_LRU_MAX_BYTES

#pragma pack(push, 1)

typedef enum BRL_HeaderFlags {
    BRL_ARCHIVE_NORMAL = 0U,
    BRL_ARCHIVE_PACKED = 1U << 0,
} BRL_HeaderFlags;

typedef struct {
    char     signature[2];            // "AE"
    uint16_t version;                 // Version
    uint32_t flags;                   // Header flags (BRL_HeaderFlags)
    uint64_t file_count;              // Active file count
    uint64_t virtual_capacity;        // Active mapped virtual space size
    uint64_t high_water_mark;         // Logical EOF
    uint64_t index_offset;            // Offset to uint64_t hash array
    uint32_t index_capacity;          // Number of index slots
    uint32_t reserved;                // Padding to keep uint64_t alignment
    uint64_t packed_virtual_capacity; // Target virtual size to restore when unpacked
    uint64_t hints;                   // Reader hints
    uint64_t variant;                 // Variant
} BRL_DiskHeader;

typedef enum BRL_EntryFlags {
    BRL_ENTRY_ACTIVE     = 1U << 0,
    BRL_ENTRY_COMPRESSED = 1U << 1,
} BRL_EntryFlags;

typedef struct {
    uint64_t offset;
    uint64_t size;
    uint64_t compressed_size;
    uint64_t allocated_size;
    uint32_t flags;
    uint32_t reserved;
} BRL_EntryMeta;

#pragma pack(pop)

typedef uint64_t (*BRL_CompressFn)(
    const void* src, uint64_t src_size, 
    void* dst, uint64_t dst_capacity, 
    uint64_t hash, void* user_data
);
typedef uint64_t (*BRL_DecompressFn)(
    const void* src, uint64_t src_size, 
    void* dst, uint64_t dst_capacity, 
    uint64_t hash, void* user_data
);
typedef uint64_t (*BRL_GetBoundFn)(uint64_t src_size, void* user_data);

typedef struct BRL_Compressor {
    bool             valid;
    BRL_CompressFn   compress;
    BRL_DecompressFn decompress;
    BRL_GetBoundFn   get_bound;
    void*            user_data;
} BRL_Compressor;

typedef struct {
    uint64_t offset;
    uint64_t size;
} BRL_OrphanHole;

typedef struct {
    BRL_OrphanHole* holes;
    uint32_t        count;
    uint32_t        capacity;
} BRL_FreeBin;

typedef struct BRL_CacheNode {
    uint64_t hash;
    void*    data;
    uint64_t size; 
    
    struct BRL_CacheNode* prev;
    struct BRL_CacheNode* next;
} BRL_CacheNode;

typedef struct BRL_DataLRU {
    BRL_CacheNode** hash_table;
    uint32_t        capacity; 
    
    BRL_CacheNode*  head;     
    BRL_CacheNode*  tail;     
    
    uint64_t        current_bytes;
    uint64_t        max_memory_bytes;
} BRL_DataLRU;

typedef enum BRL_OpenFlags {
    BRL_OPEN_NORMAL                      = 1U << 0,
    BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE = 1U << 1,
} BRL_OpenFlags;

typedef struct BRL_Archive {
    BRL_fd            fd;
    BRL_DiskHeader*   header;
    uint8_t*          mapped_data;
    uint64_t          mapped_size;

    // Structure of Arrays (SoA) Index
    uint64_t*         hashes;
    BRL_EntryMeta*    metadata;
    uint32_t          index_capacity;

    // Segregated Free Lists
    BRL_FreeBin       free_bins[BRL_NUM_BINS];

    // Reusable Compressor Scratch Arena
    BRL_Compressor    compressor;
    void*             comp_buffer;
    uint64_t          comp_capacity;

    // Dirty Range Tracking
    uint64_t          dirty_min;
    uint64_t          dirty_max;
    bool              is_dirty;

    // LRU caches
    BRL_DataLRU       compressor_lru;

    uint32_t          open_flags;
} BRL_Archive;

#ifndef BRL_DISABLE_STATIC_LEN_CHECKS
BRL_STATIC_ASSERT(sizeof(BRL_DiskHeader) == 72, BRL_DiskHeader_size);
BRL_STATIC_ASSERT(sizeof(BRL_EntryMeta) == 40, BRL_EntryMeta_size);
#endif // BRL_DISABLE_STATIC_LEN_CHECKS

BRL_EXTERN_C_END
