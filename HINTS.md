## Hints 

### Overview

Hints are lightweight metadata tags attached to a Barrel archive header. They inform reading and extracting software about any transformations applied to an entry's data, such as compression, encryption, or custom encoding, without requiring the parser to inspect or guess the payload format beforehand.

By adhering to standardized hints, independent tools can reliably recognize entry features, invoke the correct callbacks, or display friendly warnings if a specific transformation (like a custom cipher) is unsupported.

### Formatting Rules
1. Fixed Length: Every individual hint identifier must be exactly 16 characters long.
    - If an identifier name is shorter than 16 characters, pad it with spaces (here shown as underscores).
    - Example: `LZ4_____`, `AES256__`, `ZSTD-L3_`.
2. Combining Multiple Hints: When an entry undergoes multiple operations (for example, compressing before encrypting), hints are chained together sequentially using dashes (`-`).
    - Syntax: `HINTONE-HINTTWO` (pad at the end if needed)
    - Order: Hints should be listed in the order the operations were applied during archive creation (or in the order they must be reversed during extraction).
3. Levels: If the algorithm supports multiple levels of compression, include it in the name separating name and level using a dash (`-`).
    - Example: `ZSTD-L3`
4. Use spaces instead of underscores. Underscores are used only to show the padding, a compliant hint should only use ASCII spaces as padding. 
5. Always convert hints to either uppercase or lowercase to avoid a program not recognizing a format from typos like `lz4` or `Lz4`.

### Compression Hints

| Hint Identifier | Description |
| :--- | :--- |
| `STORE___________` | Stored as-is (nothing applied) |
| `DEF-L1__________` | Deflate (Fastest / Level 1) |
| `DEF-L6__________` | Deflate (Default / Level 6) |
| `DEF-L9__________` | Deflate (Max / Level 9) |
| `LZ4_____________` | Standard LZ4 |
| `LZ4-HC__________` | LZ4 High Compression |
| `ZSTD-L1_________` | Zstandard (Fastest / Level 1) |
| `ZSTD-L3_________` | Zstandard (Default / Level 3) |
| `ZSTD-L19________` | Zstandard (High / Level 19) |
| `ZSTD-L22________` | Zstandard (Ultra / Level 22) |
| `GZ-L1___________` | Gzip (Fastest / Level 1) |
| `GZ-L6___________` | Gzip (Default / Level 6) |
| `GZ-L9___________` | Gzip (Max / Level 9) |
| `BZ2-L1__________` | Bzip2 (Fastest / Level 1) |
| `BZ2-L9__________` | Bzip2 (Max / Level 9) |
| `XZ-L1___________` | XZ/LZMA2 (Fastest / Level 1) |
| `XZ-L6___________` | XZ/LZMA2 (Default / Level 6) |
| `XZ-L9___________` | XZ/LZMA2 (Max / Level 9) |
| `BR-L1___________` | Brotli (Fastest / Level 1) |
| `BR-L4___________` | Brotli (Default / Level 4) |
| `BR-L11__________` | Brotli (Max / Level 11) |
| `SNAPPY__________` | Snappy compression |
| `LZO-1X__________` | LZO-1X compression |

---

### Encryption Hints

| Hint Identifier | Description |
| :--- | :--- |
| `AES128-CBC______` | AES-128 (CBC mode) |
| `AES128-GCM______` | AES-128 (GCM authenticated) |
| `AES192-GCM______` | AES-192 (GCM authenticated) |
| `AES256-CBC______` | AES-256 (CBC mode) |
| `AES256-GCM______` | AES-256 (GCM authenticated) |
| `AES256-CTR______` | AES-256 (CTR mode) |
| `CHACHA20________` | Raw ChaCha20 stream cipher |
| `CC20-POLY_______` | ChaCha20-Poly1305 (AEAD) |
| `XCC20-POLY______` | XChaCha20-Poly1305 (AEAD) |
| `3DES-CBC________` | Triple-DES (CBC mode) |
| `BLOWFISH________` | Blowfish cipher |
| `TWOFISH256______` | Twofish-256 |
| `SERPENT256______` | Serpent-256 |
| `AGE-X25519______` | Age (X25519 curve) |

---

### Checksums & Hashes

| Hint Identifier | Description |
| :--- | :--- |
| `CRC32__________` | 32-bit CRC checksum |
| `CRC64__________` | 64-bit CRC checksum |
| `XXH32__________` | xxHash 32-bit |
| `XXH64__________` | xxHash 64-bit |
| `XXH3___________` | xxHash3 (64/128-bit) |
| `ADLER32________` | Adler-32 checksum |
| `MD5____________` | MD5 hash |
| `SHA1___________` | SHA-1 hash |
| `SHA256_________` | SHA-256 hash |
| `SHA512_________` | SHA-512 hash |
| `SHA3-256_______` | SHA3-256 hash |
| `SHA3-512_______` | SHA3-512 hash |
| `BLAKE2B________` | BLAKE2b (512-bit) |
| `BLAKE2S________` | BLAKE2s (256-bit) |
| `BLAKE3_________` | BLAKE3 tree hash |

---

### Serialization & Encodings

| Hint Identifier | Description |
| :--- | :--- |
| `UTF8___________` | UTF-8 text |
| `UTF16LE________` | UTF-16 Little Endian text |
| `B64____________` | Standard Base64 |
| `B64URL_________` | URL-safe Base64 |
| `HEX____________` | Hexadecimal (Base16) |
| `JSON___________` | JSON document |
| `BSON___________` | Binary JSON |
| `CBOR___________` | CBOR binary data |
| `MSGPACK________` | MessagePack binary |
| `PROTOBUF_______` | Protocol Buffers |
| `FLATBUFF_______` | FlatBuffers |
| `AVRO___________` | Apache Avro |

---

*Don't see the encoding you use? Open a PR and request it to be added to the standard hints!*
