# Hints

### Overview

Hints are lightweight 64-bit bitfield headers attached to a Barrel archive entry. Instead of string parsing, reader software can determine whether a specific compression, encryption, hashing, or encoding algorithm was applied by checking individual flag bits via fast bitwise operators (`&`, `|`, `~`).

When multiple transformations are applied to an entry (for example, **LZ4 + AES256-GCM**), their respective flag bits are combined into a single 64-bit integer using a bitwise OR (`|`).

---

### Bit Banging Examples

#### Writing Hints (Setting Flags)

```c
uint64_t hint = HINT1 | HINT2;

```

#### Reading Hints (Testing for Features)

```c
// Check if LZ4 is present
if (hint & HINT_COMP_LZ4) {
    // LZ4 compression is present (Result > 0)
}

// Check if AES256-GCM is present
if (hint & HINT_ENC_AES256_GCM) {
    // AES256-GCM encryption is present
}

// Quick check if ANY compression is applied
if (hint & HINT_CAT_COMPRESSION_MASK) {
    // Has at least one compression algorithm
}

```

### Category Bit Ranges

| Bit Range | Domain | Category Mask |
| --- | --- | --- |
| **Bits 0–15** | Compression Algorithms | `0x000000000000FFFF` |
| **Bits 16–31** | Encryption Ciphers | `0x00000000FFFF0000` |
| **Bits 32–47** | Checksums & Hashes | `0x0000FFFF00000000` |
| **Bits 48–63** | Serialization & Encodings | `0xFFFF000000000000` |

### Compression Hints

| Alg Name | Hex Code (`uint64_t`) | Description |
| --- | --- | --- |
| `STORE` | `0x0000000000000000` | Stored as-is (no flags set) |
| `DEF` | `0x0000000000000001` | Deflate |
| `LZ4` | `0x0000000000000002` | Standard LZ4 |
| `LZ4-HC` | `0x0000000000000004` | LZ4 High Compression |
| `ZSTD` | `0x0000000000000008` | Zstandard |
| `GZ` | `0x0000000000000010` | Gzip |
| `BZ2` | `0x0000000000000020` | Bzip2 |
| `XZ` | `0x0000000000000040` | XZ / LZMA2 |
| `BR` | `0x0000000000000080` | Brotli |
| `SNAPPY` | `0x0000000000000100` | Snappy compression |
| `LZO-1X` | `0x0000000000000200` | LZO-1X compression |

### Encryption Hints

| Alg Name | Hex Code (`uint64_t`) | Description |
| --- | --- | --- |
| `AES128-CBC` | `0x0000000000010000` | AES-128 (CBC mode) |
| `AES128-GCM` | `0x0000000000020000` | AES-128 (GCM authenticated) |
| `AES192-GCM` | `0x0000000000040000` | AES-192 (GCM authenticated) |
| `AES256-CBC` | `0x0000000000080000` | AES-256 (CBC mode) |
| `AES256-GCM` | `0x0000000000100000` | AES-256 (GCM authenticated) |
| `AES256-CTR` | `0x0000000000200000` | AES-256 (CTR mode) |
| `CHACHA20` | `0x0000000000400000` | Raw ChaCha20 stream cipher |
| `CC20-POLY` | `0x0000000000800000` | ChaCha20-Poly1305 (AEAD) |
| `XCC20-POLY` | `0x0000000001000000` | XChaCha20-Poly1305 (AEAD) |
| `3DES-CBC` | `0x0000000002000000` | Triple-DES (CBC mode) |
| `BLOWFISH` | `0x0000000004000000` | Blowfish cipher |
| `TWOFISH256` | `0x0000000008000000` | Twofish-256 |
| `SERPENT256` | `0x0000000010000000` | Serpent-256 |
| `AGE-X25519` | `0x0000000020000000` | Age (X25519 curve) |

### Checksums & Hashes

| Alg Name | Hex Code (`uint64_t`) | Description |
| --- | --- | --- |
| `CRC32` | `0x0000000100000000` | 32-bit CRC checksum |
| `CRC64` | `0x0000000200000000` | 64-bit CRC checksum |
| `XXH32` | `0x0000000400000000` | xxHash 32-bit |
| `XXH64` | `0x0000000800000000` | xxHash 64-bit |
| `XXH3` | `0x0000001000000000` | xxHash3 (64/128-bit) |
| `ADLER32` | `0x0000002000000000` | Adler-32 checksum |
| `MD5` | `0x0000004000000000` | MD5 hash |
| `SHA1` | `0x0000008000000000` | SHA-1 hash |
| `SHA256` | `0x0000010000000000` | SHA-256 hash |
| `SHA512` | `0x0000020000000000` | SHA-512 hash |
| `SHA3-256` | `0x0000040000000000` | SHA3-256 hash |
| `SHA3-512` | `0x0000080000000000` | SHA3-512 hash |
| `BLAKE2B` | `0x0000100000000000` | BLAKE2b (512-bit) |
| `BLAKE2S` | `0x0000200000000000` | BLAKE2s (256-bit) |
| `BLAKE3` | `0x0000400000000000` | BLAKE3 tree hash |

### Serialization & Encodings

| Alg Name | Hex Code (`uint64_t`) | Description |
| --- | --- | --- |
| `UTF8` | `0x0001000000000000` | UTF-8 text |
| `UTF16LE` | `0x0002000000000000` | UTF-16 Little Endian text |
| `B64` | `0x0004000000000000` | Standard Base64 |
| `B64URL` | `0x0008000000000000` | URL-safe Base64 |
| `EXECUTABLE` | `0x0010000000000000` | Executable code |
| `JSON` | `0x0020000000000000` | JSON document |
| `BSON` | `0x0040000000000000` | Binary JSON |
| `CBOR` | `0x0080000000000000` | CBOR binary data |
| `MSGPACK` | `0x0100000000000000` | MessagePack binary |
| `PROTOBUF` | `0x0200000000000000` | Protocol Buffers |
| `FLATBUFF` | `0x0400000000000000` | FlatBuffers |
| `AVRO` | `0x0800000000000000` | Apache Avro |

---

*Don't see the encoding you use? Open a PR and request it to be added to the standard hints!*
