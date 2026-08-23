import zlib
from pybarrel import Archive, OpenFlags, Error

ARCHIVE_NAME = "arch.brl"
HINTS        = 0x0000000000000008

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

Archive.create(ARCHIVE_NAME)

with Archive(ARCHIVE_NAME, OpenFlags.NORMAL) as arch:
    arch.set_compressor(py_compress, py_decompress, py_get_bound)
    
    print("Writing compressed data")
    data_in = b"Hello, World!" * 100
    arch.write("my_key", data_in, compress=True)
    
    print("Decompressing and reading data")
    data_out = arch.read("my_key")
    print(bytes(data_out)) 
    
    if data_in == data_out:
        print("OK!")
    else:
        print("in and out did not match")
        