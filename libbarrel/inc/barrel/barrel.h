#pragma once
#include "brlcompfix.h"
#include "barrel/brlapi.h"
#include "brldef.h"

BRL_EXTERN_C_START

typedef enum BRL_Error {
    BRL_OK,                                         // Success
    BRL_INVALID_PARAM,                              // An invalid parameter was provided

    BRL_INVALID_FD,                                 // An invalid file descriptor was provided
    BRL_INVALID_FILE_SIZE,                          // Invalid file size
    BRL_READ_FAIL,                                  // General read failure
    BRL_WRITE_FAIL,                                 // General write failure

    BRL_INVALID_HEADER,                             // Wrong header
    BRL_INVALID_MAGIC,                              // Wrong magic: cur[0] != 'A' OR cur[1] != 'E' 
    BRL_INVALID_VERSION,                            // The archive version is invalid or incompatible 
    BRL_INVALID_IDX_CAPACITY,                       // Index capacity in header failed safety checks
    BRL_INVALID_IDX_OFFSET,                         // Index offset in header failed safety checks
    BRL_HI_WATER_MARK_LESS_THAN_IDX_OFFSET,         // Water mark in header is less than index offset
    BRL_HI_WATER_MARK_MORE_THAN_FILE_SIZE,          // Water mark in header is more than file size
    BRL_HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY,   // Water mark in header is more than virtual capacity
    BRL_INVALID_INDEX_BYTES,                        // The calculated index bytes are larger than the file size
    BRL_VIRTUAL_CAPACITY_MORE_THAN_MAX,             // Virtual capacity exceeds the maximum allowed by Barrel

    BRL_HEADER_WRITE_FAIL,                          // Failure to write header
    BRL_ENTRY_READ_FAIL,                            // Failure to read an entry
    BRL_ENTRY_WRITE_FAIL,                           // Failure to write an entry
    BRL_SPARSE_ALLOC_FAIL,                          // Sparse allocation failed

    BRL_ALLOC_FAIL,                                 // Allocation failed
    BRL_MMAP_FAIL,                                  // Memory mapping failed
    BRL_MSYNC_FAIL,                                 // Memory sync failed

    BRL_ENTRY_NOT_FOUND,                            // The requested entry was not found
    BRL_NO_SLOT_AVAILABLE,                          // No more slots available

    BRL_NO_DECOMPRESSOR,                            // Barrel encountered a compressed entry, but no compressor was set
    BRL_DECOMPRESSOR_CALLBACK_FAILED,               // Barrel tried to compress/decompress an entry, but the callback returned an error
    BRL_REQUIRES_DECOMPRESSION,                     // Entry is compressed on disk; use BRL_ReadCopy
    BRL_BUFFER_TOO_SMALL,                           // The provided destination buffer is too small

    BRL_UNKNOWN,                                    // Unused
} BRL_Error;

// Creates a new empty Barrel file on disk
BRL_Error       BRLAPI BRL_Create(const char* filepath, uint64_t hints, uint64_t initial_index_capacity, uint64_t max_virtual_capacity);

// Opens and mmaps a Barrel file, building in-memory O(1) structures
BRL_Error       BRLAPI BRL_Open(const char* filepath, BRL_Archive** out_arch);

// Flushes changes and unmaps the archive
BRL_Error       BRLAPI BRL_Close(BRL_Archive* arch);

// Returns direct mapped pointer to data
BRL_Error       BRLAPI BRL_Read(BRL_Archive* arch, uint64_t hash, const uint8_t** out_data, uint64_t* out_size);

// Read an entry and copy it in a buffer, used for compressed entries
BRL_Error       BRL_ReadCopy(BRL_Archive* arch, uint64_t hash, 
    void* dst_buffer, uint64_t dst_capacity, uint64_t* out_written_size);

// Writes data into existing hole, overwrites in-place, or appends
BRL_Error       BRLAPI BRL_WriteEx(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size, bool use_compressor);

// Writes data into existing hole, overwrites in-place, or appends
BRL_Error       BRLAPI BRL_Write(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size);

// Deletes a file entry and marks its region as an orphan hole
BRL_Error       BRLAPI BRL_Delete(BRL_Archive* arch, uint64_t hash);

// Synchronizes mmap memory to disk
BRL_Error       BRLAPI BRL_Sync(BRL_Archive* arch);

// Hash a string, used to locate entries by original name
uint64_t        BRLAPI BRL_HashString(const char* str);

// Get the current compressor, NULL if not set
BRL_Compressor* BRLAPI BRL_GetDecompressor(BRL_Archive* arch);

// Set a compressor. false if arch or compressor is NULL
bool            BRLAPI BRL_SetCompressor(BRL_Archive* arch, const BRL_Compressor* compressor);

// Formats the error code into a string.
const char*     BRLAPI BRL_FormatError(BRL_Error err);

#ifndef BRL_HASH
// Hash a string, used to locate entries by original name
#define BRL_HASH(str) BRL_HashString((str))
#endif // BRL_HASH

BRL_EXTERN_C_END

// C++ extensions
#ifdef BRL_ENV_CC

#include <cstdint>
#include <string_view>

// Hash a constant string, used to locate entries and avoiding runtime hashing
constexpr uint64_t BRL_CC_HashStringConst(std::string_view str) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV offset basis
    for (char c : str) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001b3ULL;          // FNV prime
    }
    return hash;
}

// Can be used like this: "string"_BRL_Hash
constexpr uint64_t operator""_BRL_Hash(const char* str, std::size_t len) noexcept {
    return BRL_CC_HashStringConst(std::string_view(str, len));
}

#ifndef BRL_HASH_CONST
// Hash a constant string, used to locate entries and avoiding runtime hashing
#define BRL_HASH_CONST(str) BRL_CC_HashStringConst((str))
#endif // BRL_HASH_CONST

#endif // BRL_ENV_CC
