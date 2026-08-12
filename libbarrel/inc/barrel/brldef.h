#pragma once
#include "brlcompfix.h"
#include <stdint.h>
#include <stdbool.h>

// offsetof only
#ifndef BRL_DISABLE_STATIC_LEN_CHECKS 
#include <stddef.h>
#endif // BRL_DISABLE_STATIC_LEN_CHECKS

#include "brlapi.h"
#include "brlplatform.h"

BRL_EXTERN_C_START

// Barrel Signature
#define BRL_SIGNATURE_0       'A'
#define BRL_SIGNATURE_1       'E'

#define BRL_VERSION           0x0100                                      // 1.0.0

#ifndef BRL_MAX_VIRT_CAP
#define BRL_MAX_VIRT_CAP  (1ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL)  // 1 TiB of virtual capacity
#endif // BRL_MAX_VIRT_CAP

#ifndef BRL_NUM_BINS
#define BRL_NUM_BINS          16                                          // ^2 size classes: 256B to 8MB
#endif // BRL_NUM_BINS

#ifndef BRL_MIN_BIN_SHIFT
#define BRL_MIN_BIN_SHIFT     8                                           // 2^8 = 256 bytes minimum bin size
#endif // BRL_MIN_BIN_SHIFT

#ifndef BRL_EMPTY_HASH
#define BRL_EMPTY_HASH        0ULL
#endif // BRL_EMPTY_HASH

#ifndef BRL_DEF_INITIAL_IDX_CAPACITY_CAP 
#define BRL_DEF_INITIAL_IDX_CAPACITY_CAP 256ULL
#endif // BRL_DEF_INITIAL_IDX_CAPACITY_CAP

#pragma pack(push, 1)

// Reserved for later use
typedef enum BRL_HeaderFlags {
    BRL_ARCHIVE_NORMAL = 0U, // The archive is ready to be read
} BRL_HeaderFlags;

typedef struct {
    char     signature[2];      // "AE"
    uint16_t version;           // Version
    uint32_t file_count;        // Active file count
    uint64_t virtual_capacity;  // Total virtual memory address space reserved
    uint64_t high_water_mark;   // Logical EOF offset for new appends
    uint64_t index_offset;      // Offset where the disk index array begins
    uint32_t index_capacity;    // Number of index slots allocated
    uint32_t flags;             // Reserved for future use
    char     hints[16];         // Hints used by programs reading archives
    uint8_t  reserved[8];       // Reserved for future use
} BRL_DiskHeader;

typedef enum BRL_EntryFlags {
    BRL_ENTRY_FREE       = 0U,
    BRL_ENTRY_ACTIVE     = 1U << 0,
    BRL_ENTRY_COMPRESSED = 1U << 1,
    BRL_ENTRY_TOMBSTONE  = 1U << 2,
} BRL_EntryFlags;

typedef struct {
    uint64_t hash;              // Path/file hash
    uint64_t offset;            // Absolute byte offset inside .brl file
    uint64_t size;              // Actual payload size
    uint64_t compressed_size;   // Size on disk (equal to size if uncompressed)
    uint64_t allocated_size;    // Allocated slot size 
    uint32_t flags;             // Flags to keep track of the entry state
    uint32_t reserved;          // Explicit alignment padding
} BRL_DiskEntry;

#pragma pack(pop)

// Callback signature for Compression
// Must return compressed byte length, or 0 if compression failed or expanded the data.
typedef uint64_t (*BRL_CompressFn)(
    const void* src, 
    uint64_t    src_size, 
    void*       dst, 
    uint64_t    dst_capacity, 
    void*       user_data
);

// Callback signature for Decompression
// Must return decompressed byte length, or 0 on error.
typedef uint64_t (*BRL_DecompressFn)(
    const void* src, 
    uint64_t    src_size, 
    void*       dst, 
    uint64_t    dst_capacity, 
    void*       user_data
);

// Callback signature for Decompression
// Must return the bounds that the compressor uses.
typedef uint64_t (*BRL_GetBoundFn)(
    uint64_t src_size, 
    void*    user_data
);

// Compressor configuration context attached to BRL_Archive
typedef struct BRL_Compressor {
    bool             valid;
    BRL_CompressFn   compress;
    BRL_DecompressFn decompress;
    BRL_GetBoundFn   get_bound;
    void*            user_data;
} BRL_Compressor;

typedef struct {
    uint64_t offset;            // Offset to orphan block
    uint64_t size;              // Size of orphan block 
} BRL_OrphanHole;

typedef struct {
    BRL_OrphanHole* holes;      // Map to orphans
    uint32_t        count;      // Amount of orphans in map
    uint32_t        capacity;   // Capacity of orphan map
} BRL_FreeBin;

typedef struct BRL_Archive {
    BRL_fd          fd;              // OS File Descriptor
    BRL_DiskHeader* header;          // Direct pointer to byte 0 in mmap space
    uint8_t*        mapped_data;     // Root pointer of mmap region
    uint64_t        mapped_size;     // Total mapped virtual size
    
    // In-Memory Hash Table
    BRL_DiskEntry*  index;
    uint32_t        index_capacity;

    // Segregated Free Lists 
    BRL_FreeBin     free_bins[BRL_NUM_BINS];

    // Compressor state
    BRL_Compressor compressor;
} BRL_Archive;

#ifndef BRL_DISABLE_STATIC_LEN_CHECKS

// Static length checks

BRL_STATIC_ASSERT(sizeof(BRL_DiskHeader) == 64,
                  BRL_DiskHeader_size);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, signature) == 0,
                  BRL_DiskHeader_signature_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, version) == 2,
                  BRL_DiskHeader_version_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, file_count) == 4,
                  BRL_DiskHeader_file_count_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, virtual_capacity) == 8,
                  BRL_DiskHeader_virtual_capacity_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, high_water_mark) == 16,
                  BRL_DiskHeader_high_water_mark_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, index_offset) == 24,
                  BRL_DiskHeader_index_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, index_capacity) == 32,
                  BRL_DiskHeader_index_capacity_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, flags) == 36,
                  BRL_DiskHeader_flags_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskHeader, hints) == 40,
                  BRL_DiskHeader_hints_offset);
BRL_STATIC_ASSERT(sizeof(BRL_DiskEntry) == 48,
                  BRL_DiskEntry_size);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, hash) == 0,
                  BRL_DiskEntry_hash_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, offset) == 8,
                  BRL_DiskEntry_offset_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, size) == 16,
                  BRL_DiskEntry_size_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, compressed_size) == 24, 
                  BRL_DiskEntry_compressed_size_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, allocated_size) == 32,
                  BRL_DiskEntry_allocated_size_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, flags) == 40,
                  BRL_DiskEntry_flags_offset);
BRL_STATIC_ASSERT(offsetof(BRL_DiskEntry, reserved) == 44,
                  BRL_DiskEntry_reserved_offset);
BRL_STATIC_ASSERT(sizeof(BRL_HeaderFlags) == 4,
                  BRL_HeaderFlags_size);

#endif // BRL_DISABLE_STATIC_LEN_CHECKS

BRL_EXTERN_C_END
