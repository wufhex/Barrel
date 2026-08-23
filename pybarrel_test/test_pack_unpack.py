import os
import zlib
import pybarrel
from pybarrel import Archive, HeaderFlags

ARCHIVE_NAME = "test_pack_unpack.brl"

def py_compress(src_mv: memoryview, dst_mv: memoryview, hash_key: int) -> int:
    compressed = zlib.compress(src_mv.tobytes())
    dst_mv[:len(compressed)] = compressed
    return len(compressed)

def py_decompress(src_mv: memoryview, dst_mv: memoryview, hash_key: int) -> int:
    decompressed = zlib.decompress(src_mv.tobytes())
    dst_mv[:len(decompressed)] = decompressed
    return len(decompressed)

def py_get_bound(src_size: int) -> int:
    return src_size + 512

def run_pack_test():
    if os.path.exists(ARCHIVE_NAME):
        os.remove(ARCHIVE_NAME)

    Archive.create(ARCHIVE_NAME)

    with Archive(ARCHIVE_NAME) as arch:
        arch.set_compressor(py_compress, py_decompress, py_get_bound)
        data1 = b"Entry 1: " + (b"A" * 1024)
        data2 = b"Entry 2: " + (b"B" * 2048)
        data3 = b"Entry 3: " + (b"C" * 4096)

        arch.write("key1", data1, compress=False)
        arch.write("key2", data2, compress=True)
        arch.write("key3", data3, compress=False)
        
        arch.delete("key2")

    size_before_pack = os.path.getsize(ARCHIVE_NAME)

    # Pack the archive and verify in-memory flags
    with Archive(ARCHIVE_NAME) as arch:
        arch.pack()
        arch.sync()
        
        header_in_mem = arch.get_header()
        assert (header_in_mem.flags & HeaderFlags.PACKED) != 0, "BRL_ARCHIVE_PACKED flag was not set while packed"

    size_after_pack = os.path.getsize(ARCHIVE_NAME)
    assert size_after_pack < size_before_pack, "Packed size was not reduced on disk"

    # Reopen packed archive 
    with Archive(ARCHIVE_NAME) as arch:
        arch.set_compressor(py_compress, py_decompress, py_get_bound)

        # Post-open, auto-unpack should have cleared the PACKED flag
        header_post_unpack = arch.get_header()
        assert (header_post_unpack.flags & HeaderFlags.PACKED) == 0, "PACKED flag was not cleared post-open"

        # Verify data integrity post-unpack
        read1 = arch.read("key1")
        read3 = arch.read("key3")
        assert bytes(read1) == data1, "Key 1 data mismatch post-unpack"
        assert bytes(read3) == data3, "Key 3 data mismatch post-unpack"

        deleted_found = False
        try:
            arch.read("key2")
        except pybarrel.BarrelError:
            deleted_found = True
        assert deleted_found, "Key 2 should remain deleted"

    print("Pack & auto-unpack test PASSED successfully!")

if __name__ == "__main__":
    run_pack_test()
