<div align="center">
	<img src="img/mystic.png" width="200" height="200">
	<h1>Barrel</h1>
	<p>
		<b>Fast, memory-mapped binary archive format and key-value storage engine for C and C++.</b>
	</p>
	<br>
</div>

**Barrel** is a lightweight, zero-copy memory-mapped file archive and key-value store interface written in C. It relies on standard memory mapping (`mmap`) to treat files on disk as zero-overhead in-memory data structures, featuring an integrated open-addressing index, segregated-bin free list management, and user-pluggable compression.

## Key Features

* **Zero-Copy Reads (`BRL_Read`)**: Direct pointer access to file payloads via OS virtual memory.
* **In-Memory & Disk Hash Indexing**: Fast $O(1)$ entry lookup via string hashing or 64-bit integer keys.
* **Segregated Free-List Allocator**: Automatically reclaims and reuses freed disk slots using 16 size-class bins (ranging from 256 B to 8 MiB).
* **Pluggable Compression**: Set custom compression and decompression callbacks (e.g., zstd, lz4, deflate) per archive instance.
* **Compile-Time Constant Hashing**: Native C++ support for `constexpr` FNV-1a hashing and standard string literals (`"path/to/file"_BRL_Hash`).
* **Strict Memory Alignment**: Binary structs are packed and verified with compile-time assertions for binary layout predictability.

## Core Data Structures

### 1. `BRL_DiskHeader` (64 bytes)
Located at offset `0` of every `.brl` file. Defines the layout, active entry count, file offset markers, and index configuration.

### 2. `BRL_DiskEntry` (48 bytes)
Represents individual file metadata within the index array:
* `hash`: 64-bit lookup hash.
* `offset`: Absolute byte offset within the archive file.
* `size`: Uncompressed size in bytes.
* `compressed_size`: Size on disk.
* `allocated_size`: Total byte size of the underlying slot (allows in-place mutations).
* `flags`: Tracks entry states (`ACTIVE`, `COMPRESSED`, `TOMBSTONE`, `FREE`).

## API Overview

### Archive Lifecycle
```c
// Create a new archive on disk
BRL_Error BRL_Create(const char* filepath, const char* hints, uint64_t initial_index_capacity);

// Open and memory-map an existing archive
BRL_Error BRL_Open(const char* filepath, BRL_Archive** out_arch);

// Flush and unmap the archive
BRL_Error BRL_Close(BRL_Archive* arch);

// Sync dirty mmap regions to disk
BRL_Error BRL_Sync(BRL_Archive* arch);
```

### Data Access & Operations
```c
// Hash a string, used to locate entries by original name
// Macro version: BRL_HASH("str") 
uint64_t  BRL_HashString(const char* str);

// Zero-copy read (returns direct memory pointer to uncompressed data)
BRL_Error BRL_Read(BRL_Archive* arch, uint64_t hash, const uint8_t** out_data, uint64_t* out_size);

// Read into a target buffer (required for compressed entries)
BRL_Error BRL_ReadCopy(BRL_Archive* arch, uint64_t hash, void* dst_buffer, uint64_t dst_capacity, uint64_t* out_written_size);

// Write or overwrite an entry
BRL_Error BRL_Write(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size);
BRL_Error BRL_WriteEx(BRL_Archive* arch, uint64_t hash, const void* data, uint64_t size, bool use_compressor);

// Delete an entry (marks the disk region as an orphan hole for future reuse)
BRL_Error BRL_Delete(BRL_Archive* arch, uint64_t hash);
```

### C++ Only Features (C++17+)
```cpp
// Hash a constant string, used to locate entries and avoiding runtime hashing
// Macro version: BRL_HASH_CONST("str")
constexpr uint64_t BRL_CC_HashStringConst(std::string_view str) noexcept

// Can be used like this: "string"_BRL_Hash, defined in barrel.h:
constexpr uint64_t operator""_BRL_Hash(const char* str, std::size_t len) noexcept {
    return BRL_CC_HashStringConst(std::string_view(str, len));
}
```

### Compression Interface
```c
bool BRL_SetCompressor(BRL_Archive* arch, const BRL_Compressor* compressor);
BRL_Compressor* BRL_GetDecompressor(BRL_Archive* arch);
```

## Error Handling

All primary procedures return a `BRL_Error` enum code. Use `BRL_FormatError` to retrieve string descriptions for runtime validation:

```c
BRL_Error err = BRL_Open("invalid.brl", &arch);
if (err != BRL_OK) {
    printf("Error: %s\n", BRL_FormatError(err));
}
```

# License

Barrel is licensed under the MIT License. Attribution is appreciated but not required.

