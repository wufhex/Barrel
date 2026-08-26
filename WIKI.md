# Wiki 

This document explains how Barrel works and how to use it correctly. 

## How does Barrel work?

### General Structure

Barrel is a high-performance, single file binary storage engine designed for ultra fast zero copy reads, low-overhead writes, and flexible data compression.

Every Barrel archive starts with a fixed 72-byte disk header (`BRL_DiskHeader`).

```
+-----------------------------------------------------------------------+
| Disk Header (72 Bytes)                                                |
| - Signature ("AE") & Version (0x0100)                                 |
| - Header flags, File count, Virtual Capacity, High Water Mark         |
| - Index offset (0x48), Index capacity, Packed size, Hints, Variant    |
+-----------------------------------------------------------------------+
| Structure of Arrays (SoA) Index Table                                 |
| - Hashes Array   [uint64_t      * Index Capacity] (8 bytes/slot)      |
| - Metadata Array [BRL_EntryMeta * Index Capacity] (40 bytes/slot)     |
+-----------------------------------------------------------------------+
| Data Area (Payloads / Blocks)                                         |
| - Variable-length raw or compressed entries                           |
| - Segregated free spaces / orphan holes (recycled slots)              |
+-----------------------------------------------------------------------+
```

### Capacities & Dynamic Growth

Barrel distinguishes between two key capacities:
1. **Virtual Capacity (`virtual_capacity`)**: The total virtual address space reserved for the archive data on disk. If writing new entries exceeds this boundary, Barrel automatically expands the virtual capacity and remaps the file. Offline expansion/shrink is also supported via `BRL_ResizeOffline`.
2. **Index Capacity (`index_capacity`)**: The number of slots available in the Structure of Arrays open-addressing hash table. 
   - **Automatic Dynamic Expansion**: Barrel automatically grows the index capacity when the table load factor reaches $\ge 75\%$ or when all open slots are occupied. When expanding, Barrel automatically doubles the slot count ($16 \to 32 \to 64 \to \dots$), shifts data payloads forward, updates entry and free-hole offsets, and rehashes active entries.
   - **Zero Setup Headaches**: You can start an archive with a small default (e.g. 256 slots) and write thousands of files without running out of slots. If you know you are archiving a large number of files upfront, specifying a larger initial capacity (e.g., `-c 65536`) avoids intermediate rehash passes.

Hints are used to instruct other software how to handle entries. Since entries might be compressed, encrypted or encoded, software can read the reader hints field (check `HINTS.md`) to understand how to process the provided data. This heavily reduces the amount of guess work a program might do to display or handle an entry.

Barrel also supports punch hole writing, meaning the underlying OS file system **(if supported)** dynamically allocates physical blocks only when data is actively written to a slot, silently discarding unused ranges back to the disk.

The `ls` command calculates the size based on the actual amount of bytes inside the file: 
```
> ls test.brl
Permissions Size User   Date Modified Name
.rw-r--r--  2.1G wufhex 23 Aug 00:52  test.brl 
```

The `du` command instead shows the actual space that the file takes on the filesystem:
```
> du -h test.brl
32K    test.brl 
```

Amazing, right? A binary file filled with that many zeroes actually occupies just about 32KiB on the disk. Of course, this only works on the local disk, if the archive needs to be shared, all those null bytes will show up again. For this purpose, use `BRL_Pack` to collapse all the null bytes, Barrel will set the `BRL_ARCHIVE_PACKED` flag in the disk header, and will automatically unpack the file and **(if enabled/supported)** punch hole write the data back on disk.

### File Lookup 

Instead of storing keys as variable-length strings inside entries, Barrel uses 64-bit integer hashes stored in a Structure of Arrays layout at the end of the file.

Because hash arrays and metadata arrays are stored separately in memory, scanning or probing index slots fits directly inside standard CPU L1/L2 caches, resulting in near-instant lookups.

### Memory Mapping 

When you open an archive (`BRL_Open`), Barrel maps the file directly into memory space via OS primitives (mmap on POSIX / MapViewOfFile on Windows).

Memory mapping only "reserves" the quantity of bytes that a Barrel file requires. Meaning huge archives are not loaded in memory all at once. Rather the OS manages and dynamically allocates "chunks" of the archive that are being used on demand. 

Memory mapping also allows Barrel to be very efficient:
1. **Zero-Copy Reads (`BRL_Read`):** If an entry is uncompressed, Barrel returns a direct pointer to the memory-mapped offset. The OS handles paging data from disk, resulting in sub-microsecond read times.
2. **Compressed Reads (`BRL_ReadCopy`):** If an entry is compressed, Barrel reads the payload and passes it through the registered decompression callback into a buffer provided by the caller. 

### Compressor Callbacks

Compressor callbacks are designed to process data when a read or write call happens. Unlike the name suggests, these callbacks can be used for any data processing, such as encoding/decoding, compression/decompression, encryption/decryption and more. 

These callbacks are designed to process data that requires external processing efficiently. For example, instead of compressing an entire archive, which would require to decompress the whole archive before opening it, Barrel allows you to compress each entry, keeping the disk header intact and avoiding wasting time on decompressing data that the program doesn't use. 

Calling `BRL_Open()` with the `BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE` open flag enables a LRU cache for these entries. This way, when an entry is processed by a callback once, the readable data is cached and stays in memory until the LRU cache is exhausted, bypassing the callback entirely. Caching can only hold up to 64MiB of data by default, files larger than that won't be cached. 

Callbacks speed heavily depend on the algorithm/s used and it's configuration. Heavier algorithms can slow down Barrel reads/writes. 

### Segregated Free Lists (Space Recycling)

When an entry is overwritten or deleted, its space isn't wasted. Barrel uses 16 Segregated Free Bins based on power-of-two size classes: 

* Small deleted spaces are sorted into smaller bins.
* Large deleted spaces are sorted into larger bins.

When writing new data, Barrel checks the appropriate bin for a reusable offset before extending the file's High Water Mark. This minimizes file fragmentation during heavy update/delete workloads.

## How do I use Barrel? 

Barrel's API is designed to be simple, clean, and efficient. An archive can be created in just a few lines of code. 

### API Structure

All core Barrel's definitions are located in `barrel/brldef.h`.
All public functions are defined in `barrel/barrel.h`

Barrel is a platform agonistic API, meaning you can easily change Barrel's default values or function implementations.

#### Overridable Macros:
```c
brldef.h:

#ifndef BRL_NUM_BINS
#define BRL_NUM_BINS          16
#endif // BRL_NUM_BINS

#ifndef BRL_MIN_BIN_SHIFT
#define BRL_MIN_BIN_SHIFT     8
#endif // BRL_MIN_BIN_SHIFT

// Magic hashes for SoA logic
#define BRL_EMPTY_HASH        0ULL
#define BRL_TOMBSTONE_HASH    1ULL

#ifndef BRL_DEF_INITIAL_IDX_CAPACITY_CAP 
#define BRL_DEF_INITIAL_IDX_CAPACITY_CAP 256ULL
#endif // BRL_DEF_INITIAL_IDX_CAPACITY_CAP

#ifndef BRL_MSYNC_PAGE_MASK
#define BRL_MSYNC_PAGE_MASK 4095ULL
#endif // BRL_MSYNC_PAGE_MASK

#ifndef BRL_COMPRESSOR_LRU_MAX_BYTES 
#define BRL_COMPRESSOR_LRU_MAX_BYTES 64 * 1024 * 1024ULL
#endif // BRL_COMPRESSOR_LRU_MAX_BYTES
```

```c
barrel.h:

#ifndef BRL_HASH
// Hash a string, used to locate entries by original name
#define BRL_HASH(str) BRL_HashString((str))
#endif // BRL_HASH

#ifndef BRL_HASH_CONST
// Hash a constant string, used to locate entries and avoiding runtime hashing
#define BRL_HASH_CONST(str) BRL_CC_HashStringConst((str))
#endif // BRL_HASH_CONST
```

#### Overridable Functions:

```c 
#ifndef BRL_MALLOC
#endif // BRL_MALLOC

#ifndef BRL_CALLOC
#endif // BRL_CALLOC

#ifndef BRL_REALLOC
#endif // BRL_REALLOC

#ifndef BRL_MEMCPY
#endif

#ifndef BRL_MEMMOVE
#endif // BRL_MEMMOVE

#ifndef BRL_MEMSET
#endif // BRL_MEMSET

#ifndef BRL_FREE
#endif // BRL_FREE

#ifndef BRL_fd
#endif // BRL_fd

#ifndef BRL_IS_INVALID_FD
#endif // BRL_IS_INVALID_FD

#ifndef BRL_CLZLL
#endif // BRL_CLZLL

#ifndef BRL_FOPEN_CREATE
#endif // BRL_FOPEN_CREATE

#ifndef BRL_FOPEN
#endif // BRL_FOPEN

#ifndef BRL_FWRITE
#endif // BRL_FWRITE

#ifndef BRL_FCLOSE
#endif // BRL_FCLOSE

#ifndef BRL_FSIZE
#endif // BRL_FSIZE

#ifndef BRL_PREAD
#endif // BRL_PREAD

#ifndef BRL_FTRUNCATE
#endif // BRL_FTRUNCATE

#ifndef BRL_EnsureSparseAlloc
#endif // BRL_EnsureSparseAlloc

#ifndef BRL_MMAP
#endif // BRL_MMAP

#ifndef BRL_MUNMAP
#endif // BRL_MUNMAP

#ifndef BRL_MSYNC
#endif // BRL_MSYNC

#ifndef BRL_PUNCH_HOLE
#endif // BRL_PUNCH_HOLE
```

#### Configuration Macros (If defined = Enabled):

```
BRL_DISABLE_STATIC_LEN_CHECKS --> Disable static length checks for Barrel's file structures.
BRL_DISABLE_PUNCH_HOLE        --> Disable punch hole functionality.
BRL_DISABLE_COMPILER_FIXES    --> Disable compiler fixes (check brlcompfix.h)
```

### Errors

Most Barrel functions return a `BRL_Error` enum value. These values are defined in `barrel/barrel.h`:

```c
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

    BRL_RESIZE_SIZE_TOO_SMALL,                      // The provided size for resize is too small
    BRL_RESIZE_DATA_TRUNCATION,                     // The provided size for resize will truncate existing data

    BRL_UNKNOWN,                                    // Unused
} BRL_Error;
```

Error codes can also be converted in human readable format by calling `BRL_FormatError`.

Signature: 
```c
BRLAPI 
const char* 
BRL_FormatError(
    BRL_Error err
);
```

The formatted errors are located in `brlerror_string.h` and are defined as follows:

```c
static const char* const __g_BRL_ErrorString[] = {
    "The operation completed successfully.",            // BRL_OK
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

    "The provided size for resize is too small",         // BRL_RESIZE_SIZE_TOO_SMALL
    "The provided size for resize will truncate existing data", // BRL_RESIZE_DATA_TRUNCATION

    "Unknown error. I don't even know how you got this error :P" // BRL_UNKNOWN 
};
```

### Creating an Archive 

Before opening an archive, it must be initialized on disk using `BRL_Create`. This sets up the `BRL_DiskHeader`, initializes the SoA index table, and sparse allocates the file up to the specified virtual capacity.

Signature:
```c
BRLAPI 
BRL_Error 
BRL_Create(
    // Path where the new archive file will be created.
    const char* filepath,
    // Bitmask flags describing expected payload types or reader instructions. Standard codes defined in HINTS.md
    uint64_t    hints,     
    // Initial number of slot entries reserved in the SoA index table.
    uint64_t    initial_index_capacity, 
    // Maximum size (in bytes) the archive can grow to on disk.
    uint64_t    max_virtual_capacity
);
```

`initial_index_capacity` sets the starting number of index slots (must be a power of 2, or 0 to use `BRL_DEF_INITIAL_IDX_CAPACITY_CAP`). Note that Barrel will automatically expand this capacity as files are added, but choosing an appropriate initial size upfront avoids runtime rehashing passes.

Example:
```c
#include "barrel/barrel.h"
#include <stdio.h>

int main(void) {
    // Reserve initial space for 256 entries and up to 1 GB virtual capacity
    BRL_Error err = BRL_Create("data.brl", 0, BRL_DEF_INITIAL_IDX_CAPACITY_CAP, 1024 * 1024 * 1024ULL);
    
    if (err != BRL_OK) {
        printf("Failed to create archive: %s\n", BRL_FormatError(err));
        return 1;
    }
    
    printf("Archive created successfully.\n");
    return 0;
}
```

### Opening and Closing an Archive

Once created, open the file using `BRL_Open`. This maps the file into memory and populates the runtime handle `BRL_Archive`. When finished, `BRL_Close` flushes unwritten changes and unmaps the file.

Signature:
```c
BRLAPI
BRL_Error 
BRL_Open(
    // Path to the archive file.
    const char* filepath, 
    // Open flags.
    uint32_t open_flags,
    // Output handle to archive. 
    BRL_Archive** out_arch
);
```

Example:
```c
BRL_Archive* arch = NULL;

// Open archive
BRL_Error err = BRL_Open("data.brl", 0, &arch);
if (err != BRL_OK) {
    printf("Open error: %s\n", BRL_FormatError(err));
    return 1;
}

// ... Perform operations ...

// Flush and unmap
BRL_Close(arch);
```

`open_flags` can be set to either `BRL_OPEN_NORMAL` (No compressor LRU cache) or `BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE` (64MiB compressor LRU cache). 

### Hashing

Hashed strings, are used by Barrel to locate entries quickly using a string based file name (such as "file.txt" or "/path/to/file.txt"). A string can be hashed in multiple ways:

In C:

```c
// Hash a string, used to locate entries by original name
// Macro version: BRL_HASH("str") 
uint64_t BRL_HashString(const char* str);
```

In C++ 17+, they can also be hashed at compile time:
```cpp
// Hash a constant string, used to locate entries and avoiding runtime hashing
// Macro version: BRL_HASH_CONST("str")
constexpr uint64_t BRL_CC_HashStringConst(std::string_view str) noexcept

// Can be used like this: "string"_BRL_Hash, defined in barrel.h:
constexpr uint64_t operator""_BRL_Hash(const char* str, std::size_t len) noexcept {
    return BRL_CC_HashStringConst(std::string_view(str, len));
}
```

Example:
```c
const uint8_t* read_text_ptr = NULL;
uint64_t read_text_size = 0;

BRL_Read(
    arch,
    BRL_HashString("my_file.txt"),
    &read_text_ptr,
    &read_text_size
);
```

### Compressor Callbacks

Barrel's callback system allows you to attach external data processing functions to an archive. Despite the name *compressor callbacks*, they are not limited to compression: they can be used for compression, encoding, encryption, or any other transformation that requires processing when data is written or read. 

A callback is registered through a `BRL_Compressor` structure:

```cpp
BRL_Compressor g_compressor = {
    true,                 // valid flag - reserved for barrel
    Test_CompressFunc,    // compress function
    Test_DecompressFunc,  // decompress function 
    Test_GetBoundFunc,    // get bounds function - for decompressiom
    nullptr               // user data
};

BRL_SetCompressor(arch, &g_compressor);
```

#### Compression callback

The compression callback receives the source data, its size, the destination buffer, its capacity, the entry hash, and optional user data.

```cpp
uint64_t Test_CompressFunc(
    const void* src,
    uint64_t    src_size,
    void*       dst,
    uint64_t    dst_capacity,
    uint64_t    hash,
    void*       user_data
);
```

For example, using LZ4:

```cpp
uint64_t Test_CompressFunc(
    const void* src,
    uint64_t    src_size,
    void*       dst,
    uint64_t    dst_capacity,
    uint64_t    hash,
    void*       user_data
) {
    (void)hash;
    (void)user_data;

    if (src_size > BRL_INT_MAX || dst_capacity > BRL_INT_MAX)
        return 0;

    int result = LZ4_compress_default(
        (const char*)src,
        (char*)dst,
        (int)src_size,
        (int)dst_capacity
    );

    if (result <= 0)
        return 0;

    return (uint64_t)result;
}
```

The callback should return the number of bytes written to `dst`. Returning `0` indicates that processing failed.

#### Decompression callback

The decompression callback performs the inverse operation:

```cpp
uint64_t Test_DecompressFunc(
    const void* src,
    uint64_t    src_size,
    void*       dst,
    uint64_t    dst_capacity,
    uint64_t    hash,
    void*       user_data
) {
    (void)hash;
    (void)user_data;

    if (src_size > BRL_INT_MAX || dst_capacity > BRL_INT_MAX)
        return 0;

    int result = LZ4_decompress_safe(
        (const char*)src,
        (char*)dst,
        (int)src_size,
        (int)dst_capacity
    );

    if (result < 0)
        return 0;

    return (uint64_t)result;
}
```

The return value is the number of bytes produced in the destination buffer. Returning `0` signals failure.

#### Getting the compression bound

Barrel also needs a callback that can determine the maximum amount of space required to process a particular input size:

```cpp
uint64_t Test_GetBoundFunc(
    uint64_t src_size,
    void*    user_data
) {
    (void)user_data;

    if (src_size > BRL_INT_MAX)
        return 0;

    return (uint64_t)LZ4_compressBound((int)src_size);
}
```

This allows Barrel to allocate an appropriately sized temporary buffer before invoking the compression callback.

#### Registering the callbacks

Once the archive has been opened, register the callback structure:

```cpp
BRL_Archive* arch = NULL;

BRL_Error err = BRL_Open(
    "example.brl",
    BRL_OPEN_NORMAL,
    &arch
);

if (err != BRL_OK)
    return 1;

if (!BRL_SetCompressor(arch, &g_compressor)) {
    BRL_Close(arch);
    return 1;
}
```

See how to read/write using these callbacks below. 

### Writing Entries

Use `BRL_Write` or `BRL_WriteEx` to insert or overwrite data by key. Keys are always stored as 64-bit integer hashes.

Signatures:
```c
BRLAPI 
BRL_Error 
BRL_WriteEx(
    // Handle to the open archive
    BRL_Archive* arch, 
    // Hash of the file name that needs to be written
    uint64_t hash, 
    // Pointer to the data to be written
    const void* data, 
    // Size of the data to be written
    uint64_t size,
    // Whether the compressor callback must be used to write the data 
    bool use_compressor
);

BRLAPI
BRL_Error 
BRL_Write(
    // Handle to the open archive
    BRL_Archive* arch, 
    // Hash of the file name that needs to be written
    uint64_t hash, 
    // Pointer to the data to be written
    const void* data, 
    // Size of the data to be written
    uint64_t size
);

```

Example:
```c
// Basic uncompressed write
const char* text = "Hello, Barrel!";
uint64_t key = BRL_HASH("config/welcome.txt");

BRL_Error err = BRL_Write(arch, key, text, strlen(text) + 1);
if (err != BRL_OK) {
    printf("Write failed: %s\n", BRL_FormatError(err));
}
```

If a registered compressor callback is active, use BRL_WriteEx to explicitly flag whether a payload should pass through the compressor:

Example:
```c
const bool UseCompressor = true;
BRL_WriteEx(arch, key, text, strlen(text) + 1, UseCompressor);
```

By passing true, BRL_WriteEx will call the `BRL_CompressFn` callback so it can process data accordingly before being written.

### Reading Entries

Use `BRL_Read` or `BRL_ReadCopy` to read data from a key.

Signatures:
```c
// Returns direct mapped pointer to data
BRLAPI
BRL_Error
BRL_Read(
    // Handle to the open archive
    BRL_Archive* arch, 
    // Hash of the file name that needs to be written
    uint64_t hash, 
    // Pointer to the data
    const uint8_t** out_data, 
    // Size of the data
    uint64_t* out_size
);

// Read an entry and copy it in a buffer, used for compressed entries
BRLAPI
BRL_Error 
BRL_ReadCopy(
    // Handle to the open archive
    BRL_Archive* arch, 
    // Hash of the file name that needs to be written
    uint64_t hash, 
    // Pointer to the dest buffer
    void* dst_buffer, 
    // Size of the dest buffer
    uint64_t dst_capacity, 
    // Size of the written data
    uint64_t* out_written_size
);
```

**NOTE:** `BRL_Read` will return `BRL_REQUIRES_DECOMPRESSION` if the entry needs processing via callback, always check for this error code.
**NOTE:** `BRL_Read` only checks for the `BRL_ENTRY_COMPRESSED` flag in the entry. `BRL_ReadCopy` will call the `BRL_DecompressFn` and `BRL_GetBoundFn` callbacks.

Examples:

```c
const uint8_t* read_text_ptr = NULL;
uint64_t read_text_size = 0;

// Uncompressed data
BRL_Read(
    arch,
    text_hash,
    &read_text_ptr,
    &read_text_size
);
```

```cpp
const uint8_t* read_massive_ptr = NULL;
uint64_t read_massive_size = 0;

BRL_Error err = BRL_Read(
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

    // C++ example, can use malloc in C
    uint8_t* decompressed_data = new uint8_t[read_massive_size];
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

    // ... use processed data here ...
} else if (err == BRL_OK) {
    // Data was already decompressed
}
else {
    // Error reading data
}
```

### Deleting Entries 

Entries can be deleted by calling `BRL_Delete`.

Signature:
```c
BRLAPI 
BRL_Error
BRL_Delete(
    // Handle to the open archive
    BRL_Archive* arch, 
    // Hash of the file name that needs to be deleted
    uint64_t hash
);
```

### Syncing archives to disk

Barrel files can be synced manually to disk without having to close them via `BRL_Sync`.

Signature:
```c
BRLAPI 
BRL_Error
BRL_Sync(
    // Handle to the open archive
    BRL_Archive* arch
);
```

### Resizing a Barrel archive

**NOTE:** Resizing an archive is not recommended because it can cause data loss or corruption and can only be done while the archive is not memory mapped. 

Signature:
```c
BRLAPI 
BRL_Error 
BRL_ResizeOffline(
    // Path to the barrel archive
    const char* filepath, 
    // New size
    uint64_t new_capacity
);
```

`BRL_ResizeOffline` will return `BRL_RESIZE_SIZE_TOO_SMALL` if the specified capacity is too small or `BRL_RESIZE_DATA_TRUNCATION` if the specified capacity will cause the archive to loose data. 

### Packing an Unpacking
Packing is essential to avoid sharing an archive thats 90% inflated by null bytes. To do this, simply call the `BRL_Pack` function.

Signature:
```c
BRL_Error 
BRL_Pack(
    // Path to the barrel archive
    BRL_Archive* arch
);
```

Barrel will check if the file is packed on first open and happily unpack it for you, as mentioned before, 
this will happen once, and the punch hole writing will make sure that no unused bytes waste space on the disk.  

## Remarks

This concludes our journey into the Barrel format. If you really read all this, you deserve an oscar. If there are any issues with the wiki, don't hesitate to open an Issue or PR!
