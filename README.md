<div align="center">
	<img src="img/barrel.png" width="100" height="100">
	<h1>Barrel</h1>
	<p>
		<b>Fast, memory-mapped binary archive format and key-value storage engine for C and C++.</b>
	</p>
	<br>
</div>

**Barrel** is a lightweight, zero-copy memory-mapped file archive and key-value store interface written in C. It relies on standard memory mapping (`mmap`) to treat files on disk as zero-overhead in-memory data structures, featuring an integrated open-addressing index, segregated-bin free list management, and user-pluggable compression.

Barrel archives are created with a predefined virtual capacity, this can be any uint64 number of bytes, but choosing a size larger than 1 TiB isn't recommended. If an archive requires more than that, creating multiple archives is heavily encoraged. 

Choosing a large capacity won't allocate the entire archive at once since memory mapping is used, so a large capacity value won't present problems and can also be beneficial if the final size isn't know at creation time. 

Archives could also be expanded/shrunk manually after creation, although it might take long for large archives and can corrupt the archive or the data in it if done improperly. The Barrel API provides official support for archive resizing (via `BRL_ResizeOffline`), although it only works when the file **IS NOT** memory mapped and it's currently an experimental feature.

## Key Features

* **Zero-Copy Reads (`BRL_Read`)**: Direct pointer access to file payloads via OS virtual memory.
* **In-Memory & Disk Hash Indexing**: Fast $O(1)$ entry lookup via string hashing or 64-bit integer keys.
* **Segregated Free-List Allocator**: Automatically reclaims and reuses freed disk slots using 16 size-class bins (ranging from 256 B to 8 MiB).
* **Pluggable Compression**: Set custom compression and decompression callbacks (e.g., zstd, lz4, deflate) per archive instance.
* **Compile-Time Constant Hashing**: Native C++ support for `constexpr` FNV-1a hashing and standard string literals (`"path/to/file"_BRL_Hash`).
* **Strict Memory Alignment**: Binary structs are packed and verified with compile-time assertions for binary layout predictability.

## Tips
1. **Use quick compression algorithms**: I reccomend using LZ4 or ZSTD with a reasonable compression level to avoid bottlenecks.
2. **Always set hints**: Whether you're compressing, encrypting or altering the entries format or data, always set hints when creating the archive if you care about your archive being recognizable by other programs. Check **HINTS.md** to see some standard examples.
3. **Avoid compressing every entry**: Reserve compression to very large files, even if decompression shouldn't be noticeably slow (depending on the chosen algorithm), avoid compressing smaller files their reading quick. 
4. **Avoid altering an entire archive**: While it might sound tempting to compress or encrypt the entire archive file, keep in mind that you'll have to pre and post process the archive before opening and after flushing it. This can become a problem with large archives and kind of defeats the purpose of a Barrel archive. Consider using the compressor interface for such operations, as it alters only the entries and reduces slowdowns as much as possible (although it heavily depends on the algorithm you're using).
5. **Compression callbacks can be used for everything**: Unlike the name suggests, Barrel doesn't care what happens in the callbacks, they can be used for any operation like encryption/decryption or other specific purposes, **as long as you respect the signature, use `BRL_WriteEx` with `use_compression = true`, and return an `uint64_t` value. Returning `0` is interpreted by Barrel as error and will return `BRL_DECOMPRESSOR_CALLBACK_FAILED`.**
6. **Use algorithms consistently**: As said, you can pick which chunks to alter and handle with the Compressor callbacks, `BRL_Read` can easily identify them and quickly return `BRL_REQUIRES_DECOMPRESSION` (If Tip #5 was followed correctly). Mixing algorithms for different entries can be done but requires special handling in the callbacks that Barrel doesn't natively support and that might not be natively understood by other programs (if compliant with the standard hints).

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
BRL_Error BRL_Create(const char* filepath, const char* hints, uint64_t initial_index_capacity, uint64_t max_virtual_capacity);
// Common defaults
initial_index_capacity = BRL_DEF_INITIAL_IDX_CAPACITY_CAP

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

// Resizes an archive file. Should only be used when the archive is not memory mapped.
BRLAPI BRL_Error BRL_ResizeOffline(const char* filepath, uint64_t new_capacity);
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

### Compression/Data Processing Interface
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

