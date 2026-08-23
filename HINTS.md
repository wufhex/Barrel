# Hints - Code Specification v1 

**NOTE:** The macros and constants defined here are provided for external implementation and integration with the Barrel library.

## Overview

Hints are lightweight 64-bit bitfield headers attached to a Barrel archive entry. Instead of guessing, reader software determines whether specific compression, encryption, or encoding formats were applied by checking individual flag bits using fast bitwise operators.

When multiple transformations are applied to an entry (for example, **LZ4 + AES256-GCM**), their respective flag bits are combined into a single 64-bit integer using a bitwise OR.

Each hint is defined by a unique bit position. The hexadecimal value is derived directly from that bit position.

### Pipeline Execution Order

Hints indicate **which** transformations were applied, but **not** their execution order. Implementations must follow standard processing order constraints:

* **Writing (Pipeline Output):** Encode -> Compress -> Encrypt.
* **Reading (Pipeline Input):** Decrypt -> Decompress -> Decode.

## Bitwise Operations

### Setting Flags (Writing)

```c
uint64_t hint = HINT_COMP_LZ4 | HINT_ENC_AES256_GCM;
```

### Testing Flags (Reading)

```c
// Check for specific compression
if (hint & HINT_COMP_LZ4) {
    // Process LZ4 decompression
}

// Check if ANY compression bit is set
if (hint & HINT_CAT_COMPRESSION_MASK) {
    // Decompress payload
}
```
## Category Bit Ranges

The 64-bit header is partitioned into three distinct active domains:

| Bit Range | Domain | Category Mask |
| --- | --- | --- |
| **Bits 0–15** | Compression Algorithms | `0x000000000000FFFF` |
| **Bits 16–31** | Encryption Ciphers | `0x00000000FFFF0000` |
| **Bits 32–47** | Reserved | `0x0000FFFF00000000` |
| **Bits 48–63** | Serialization & Encodings | `0xFFFF000000000000` |

### C Definitions

```c
#define HINT_BIT(n)                (UINT64_C(1) << (n))

#define HINT_CAT_COMPRESSION_MASK  UINT64_C(0x000000000000FFFF)
#define HINT_CAT_ENCRYPTION_MASK   UINT64_C(0x00000000FFFF0000)
#define HINT_CAT_ENCODING_MASK     UINT64_C(0xFFFF000000000000)
```

## Compression Hints (Bits 0–15)
| Alg Name | Bit | Code | Hex Value (`uint64_t`) | Description |
| --- | --- | --- | --- | --- |
| `STORE` | — | `UINT64_C(0)` | `0x0000000000000000` | Uncompressed / Raw payload |
| `DEF` | 0 | `HINT_BIT(0)` | `0x0000000000000001` | Deflate |
| `LZ4` | 1 | `HINT_BIT(1)` | `0x0000000000000002` | Standard LZ4 |
| `LZ4-HC` | 2 | `HINT_BIT(2)` | `0x0000000000000004` | LZ4 High Compression |
| `ZSTD` | 3 | `HINT_BIT(3)` | `0x0000000000000008` | Zstandard |
| `GZ` | 4 | `HINT_BIT(4)` | `0x0000000000000010` | Gzip |
| `BZ2` | 5 | `HINT_BIT(5)` | `0x0000000000000020` | Bzip2 |
| `XZ` | 6 | `HINT_BIT(6)` | `0x0000000000000040` | XZ / LZMA2 |
| `BR` | 7 | `HINT_BIT(7)` | `0x0000000000000080` | Brotli |
| `SNAPPY` | 8 | `HINT_BIT(8)` | `0x0000000000000100` | Snappy |
| `LZO-1X` | 9 | `HINT_BIT(9)` | `0x0000000000000200` | LZO-1X |
| `RESERVED` | 10–15 | — | — | Reserved for compression algorithms |
```c
#define HINT_COMP_STORE     UINT64_C(0)
#define HINT_COMP_DEF       HINT_BIT(0)
#define HINT_COMP_LZ4       HINT_BIT(1)
#define HINT_COMP_LZ4_HC    HINT_BIT(2)
#define HINT_COMP_ZSTD      HINT_BIT(3)
#define HINT_COMP_GZ        HINT_BIT(4)
#define HINT_COMP_BZ2       HINT_BIT(5)
#define HINT_COMP_XZ        HINT_BIT(6)
#define HINT_COMP_BR        HINT_BIT(7)
#define HINT_COMP_SNAPPY    HINT_BIT(8)
#define HINT_COMP_LZO_1X    HINT_BIT(9)
```

## Encryption Hints (Bits 16–31)

| Alg Name | Bit | Code | Hex Value (`uint64_t`) | Description |
| --- | --- | --- | --- | --- |
| `AES128-CBC` | 16 | `HINT_BIT(16)` | `0x0000000000010000` | AES-128 (CBC mode) |
| `AES128-GCM` | 17 | `HINT_BIT(17)` | `0x0000000000020000` | AES-128 (GCM authenticated) |
| `AES192-GCM` | 18 | `HINT_BIT(18)` | `0x0000000000040000` | AES-192 (GCM authenticated) |
| `AES256-CBC` | 19 | `HINT_BIT(19)` | `0x0000000000080000` | AES-256 (CBC mode) |
| `AES256-GCM` | 20 | `HINT_BIT(20)` | `0x0000000000100000` | AES-256 (GCM authenticated) |
| `AES256-CTR` | 21 | `HINT_BIT(21)` | `0x0000000000200000` | AES-256 (CTR mode) |
| `CHACHA20` | 22 | `HINT_BIT(22)` | `0x0000000000400000` | Raw ChaCha20 stream cipher |
| `CC20-POLY` | 23 | `HINT_BIT(23)` | `0x0000000000800000` | ChaCha20-Poly1305 (AEAD) |
| `XCC20-POLY` | 24 | `HINT_BIT(24)` | `0x0000000001000000` | XChaCha20-Poly1305 (AEAD) |
| `3DES-CBC` | 25 | `HINT_BIT(25)` | `0x0000000002000000` | Triple-DES (CBC mode) |
| `BLOWFISH` | 26 | `HINT_BIT(26)` | `0x0000000004000000` | Blowfish cipher |
| `TWOFISH256` | 27 | `HINT_BIT(27)` | `0x0000000008000000` | Twofish-256 |
| `SERPENT256` | 28 | `HINT_BIT(28)` | `0x0000000010000000` | Serpent-256 |
| `AGE-X25519` | 29 | `HINT_BIT(29)` | `0x0000000020000000` | Age (X25519 key exchange) |
| `RESERVED` | 30–31 | — | — | Reserved for future encryption algorithms |

```c
#define HINT_ENC_AES128_CBC  HINT_BIT(16)
#define HINT_ENC_AES128_GCM  HINT_BIT(17)
#define HINT_ENC_AES192_GCM  HINT_BIT(18)
#define HINT_ENC_AES256_CBC  HINT_BIT(19)
#define HINT_ENC_AES256_GCM  HINT_BIT(20)
#define HINT_ENC_AES256_CTR  HINT_BIT(21)
#define HINT_ENC_CHACHA20    HINT_BIT(22)
#define HINT_ENC_CC20_POLY   HINT_BIT(23)
#define HINT_ENC_XCC20_POLY  HINT_BIT(24)
#define HINT_ENC_3DES_CBC    HINT_BIT(25)
#define HINT_ENC_BLOWFISH    HINT_BIT(26)
#define HINT_ENC_TWOFISH256  HINT_BIT(27)
#define HINT_ENC_SERPENT256  HINT_BIT(28)
#define HINT_ENC_AGE_X25519  HINT_BIT(29)

```

## Serialization & Encodings Hints (Bits 48–63)

| Format Name | Bit | Code | Hex Value (`uint64_t`) | Description |
| --- | --- | --- | --- | --- |
| `UTF8` | 48 | `HINT_BIT(48)` | `0x0001000000000000` | Plain text (UTF-8) |
| `UTF16LE` | 49 | `HINT_BIT(49)` | `0x0002000000000000` | UTF-16 Little Endian text |
| `B64` | 50 | `HINT_BIT(50)` | `0x0004000000000000` | Standard Base64 |
| `B64URL` | 51 | `HINT_BIT(51)` | `0x0008000000000000` | URL-safe Base64 |
| `EXECUTABLE` | 52 | `HINT_BIT(52)` | `0x0010000000000000` | Binary executable |
| `JSON` | 53 | `HINT_BIT(53)` | `0x0020000000000000` | JSON document |
| `BSON` | 54 | `HINT_BIT(54)` | `0x0040000000000000` | Binary JSON |
| `CBOR` | 55 | `HINT_BIT(55)` | `0x0080000000000000` | CBOR binary |
| `MSGPACK` | 56 | `HINT_BIT(56)` | `0x0100000000000000` | MessagePack binary |
| `PROTOBUF` | 57 | `HINT_BIT(57)` | `0x0200000000000000` | Protocol Buffers |
| `FLATBUFF` | 58 | `HINT_BIT(58)` | `0x0400000000000000` | FlatBuffers |
| `AVRO` | 59 | `HINT_BIT(59)` | `0x0800000000000000` | Apache Avro |
| `RESERVED` | 60–63 | — | — | Reserved for future encodings |

```c
#define HINT_FMT_UTF8        HINT_BIT(48)
#define HINT_FMT_UTF16LE     HINT_BIT(49)
#define HINT_FMT_B64         HINT_BIT(50)
#define HINT_FMT_B64URL      HINT_BIT(51)
#define HINT_FMT_EXECUTABLE  HINT_BIT(52)
#define HINT_FMT_JSON        HINT_BIT(53)
#define HINT_FMT_BSON        HINT_BIT(54)
#define HINT_FMT_CBOR        HINT_BIT(55)
#define HINT_FMT_MSGPACK     HINT_BIT(56)
#define HINT_FMT_PROTOBUF    HINT_BIT(57)
#define HINT_FMT_FLATBUFF    HINT_BIT(58)
#define HINT_FMT_AVRO        HINT_BIT(59)
```

Reserved bits should remain unused until officially allocated to prevent compatibility conflicts across reader implementations.

## Remarks

If you have any algorithm that's worth adding, don't hesitate to open an Issue or PR!
