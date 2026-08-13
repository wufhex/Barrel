#pragma once

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
