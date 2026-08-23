import ctypes
from enum import IntEnum
from typing import Callable, Optional, Union

from . import _abi

BarrelError = _abi.BarrelError
hash_string = _abi.hash_string

CompressFn = Callable[[memoryview, memoryview, int], int]
DecompressFn = Callable[[memoryview, memoryview, int], int]
GetBoundFn = Callable[[int], int]

class Error(IntEnum):
    OK = _abi.BRL_OK
    INVALID_PARAM = _abi.BRL_INVALID_PARAM
    INVALID_FD = _abi.BRL_INVALID_FD
    INVALID_FILE_SIZE = _abi.BRL_INVALID_FILE_SIZE
    READ_FAIL = _abi.BRL_READ_FAIL
    WRITE_FAIL = _abi.BRL_WRITE_FAIL
    INVALID_HEADER = _abi.BRL_INVALID_HEADER
    INVALID_MAGIC = _abi.BRL_INVALID_MAGIC
    INVALID_VERSION = _abi.BRL_INVALID_VERSION
    INVALID_IDX_CAPACITY = _abi.BRL_INVALID_IDX_CAPACITY
    INVALID_IDX_OFFSET = _abi.BRL_INVALID_IDX_OFFSET
    HI_WATER_MARK_LESS_THAN_IDX_OFFSET = _abi.BRL_HI_WATER_MARK_LESS_THAN_IDX_OFFSET
    HI_WATER_MARK_MORE_THAN_FILE_SIZE = _abi.BRL_HI_WATER_MARK_MORE_THAN_FILE_SIZE
    HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY = (
        _abi.BRL_HI_WATER_MARK_MORE_THAN_VIRTUAL_CAPACITY
    )
    INVALID_INDEX_BYTES = _abi.BRL_INVALID_INDEX_BYTES
    VIRTUAL_CAPACITY_MORE_THAN_MAX = _abi.BRL_VIRTUAL_CAPACITY_MORE_THAN_MAX
    HEADER_WRITE_FAIL = _abi.BRL_HEADER_WRITE_FAIL
    ENTRY_READ_FAIL = _abi.BRL_ENTRY_READ_FAIL
    ENTRY_WRITE_FAIL = _abi.BRL_ENTRY_WRITE_FAIL
    SPARSE_ALLOC_FAIL = _abi.BRL_SPARSE_ALLOC_FAIL
    ALLOC_FAIL = _abi.BRL_ALLOC_FAIL
    MMAP_FAIL = _abi.BRL_MMAP_FAIL
    MSYNC_FAIL = _abi.BRL_MSYNC_FAIL
    ENTRY_NOT_FOUND = _abi.BRL_ENTRY_NOT_FOUND
    NO_SLOT_AVAILABLE = _abi.BRL_NO_SLOT_AVAILABLE
    NO_DECOMPRESSOR = _abi.BRL_NO_DECOMPRESSOR
    DECOMPRESSOR_CALLBACK_FAILED = _abi.BRL_DECOMPRESSOR_CALLBACK_FAILED
    REQUIRES_DECOMPRESSION = _abi.BRL_REQUIRES_DECOMPRESSION
    BUFFER_TOO_SMALL = _abi.BRL_BUFFER_TOO_SMALL
    RESIZE_SIZE_TOO_SMALL = _abi.BRL_RESIZE_SIZE_TOO_SMALL
    RESIZE_DATA_TRUNCATION = _abi.BRL_RESIZE_DATA_TRUNCATION
    UNKNOWN = _abi.BRL_UNKNOWN

class OpenFlags(IntEnum):
    NORMAL = _abi.BRL_OPEN_NORMAL
    ENABLE_COMPRESSOR_LRU_CACHE = _abi.BRL_OPEN_ENABLE_COMPRESSOR_LRU_CACHE

class EntryFlags(IntEnum):
    ACTIVE = _abi.BRL_ENTRY_ACTIVE
    COMPRESSED = _abi.BRL_ENTRY_COMPRESSED

class HeaderFlags(IntEnum):
    NORMAL = _abi.BRL_ARCHIVE_NORMAL
    PACKED = _abi.BRL_ARCHIVE_PACKED

class BRL_DiskHeader(ctypes.LittleEndianStructure):
    _pack_ = 1
    _layout_ = "ms"
    _fields_ = [
        ("signature", ctypes.c_char * 2),
        ("version", ctypes.c_uint16),
        ("flags", ctypes.c_uint32),
        ("file_count", ctypes.c_uint64),
        ("virtual_capacity", ctypes.c_uint64),
        ("high_water_mark", ctypes.c_uint64),
        ("index_offset", ctypes.c_uint64),
        ("index_capacity", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("packed_virtual_capacity", ctypes.c_uint64),
        ("hints", ctypes.c_uint64),
        ("variant", ctypes.c_uint64),
    ]

class BRL_EntryMeta(ctypes.LittleEndianStructure):
    _pack_ = 1
    _layout_ = "ms"
    _fields_ = [
        ("offset", ctypes.c_uint64),
        ("size", ctypes.c_uint64),
        ("compressed_size", ctypes.c_uint64),
        ("allocated_size", ctypes.c_uint64),
        ("flags", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
    ]

assert ctypes.sizeof(BRL_DiskHeader) == 72, "BRL_DiskHeader size mismatch"
assert ctypes.sizeof(BRL_EntryMeta) == 40, "BRL_EntryMeta size mismatch"

class Archive:
    def __init__(self, filepath: str, open_flags: int = OpenFlags.NORMAL):
        self.filepath = filepath
        self._handle = _abi.open(filepath, open_flags)

    @classmethod
    def create(
        cls,
        filepath: str,
        hints: int = 0,
        initial_capacity: int = 256,
        max_virtual_capacity: int = 16 << 20,
    ) -> None:
        """Creates a new empty Barrel archive on disk."""
        _abi.create(filepath, hints, initial_capacity, max_virtual_capacity)

    def set_compressor(
        self,
        compress: CompressFn,
        decompress: DecompressFn,
        get_bound: Optional[GetBoundFn] = None,
    ) -> None:
        """Sets custom compression callbacks on the archive handle."""
        if get_bound is None:
            get_bound = lambda src_size: src_size * 2 + 128
        _abi.set_compressor(self._handle, compress, decompress, get_bound)

    def _resolve_hash(self, key: Union[str, int]) -> int:
        if isinstance(key, str):
            return hash_string(key)
        return key

    def read(self, key: Union[str, int]) -> memoryview:
        """Zero-copy read. If compressed, falls back to read_copy."""
        key_hash = self._resolve_hash(key)
        try:
            return _abi.read(self._handle, key_hash)
        except _abi.BarrelError as e:
            if getattr(e, "code", None) == Error.REQUIRES_DECOMPRESSION:
                return memoryview(self.read_copy(key_hash))
            raise e

    def read_hash(self, key_hash: int) -> memoryview:
        """Zero-copy read by hash. If compressed, falls back to read_copy."""
        try:
            return _abi.read(self._handle, key_hash)
        except _abi.BarrelError as e:
            if getattr(e, "code", None) == Error.REQUIRES_DECOMPRESSION:
                return memoryview(self.read_copy(key_hash))
            raise

    def read_copy(self, key: Union[str, int], expected_max_size: int = 10 * 1024 * 1024) -> bytearray:
        """Reads compressed or uncompressed entry into a bytearray allocation."""
        return _abi.read_copy(self._handle, self._resolve_hash(key), expected_max_size)

    def write(self, key: Union[str, int], data: bytes, compress: bool = False) -> None:
        """Writes data into the archive under the designated string or integer key."""
        _abi.write_ex(self._handle, self._resolve_hash(key), data, compress)

    def pack(self) -> None:
        """Packs the archive to reclaim fragmented space and truncate the file."""
        if self._handle is not None:
            _abi.pack(self._handle)

    @classmethod
    def resize_offline(cls, filepath: str, new_size: int) -> None:
        """Resizes a Barrel archive offline"""
        _abi.resize_offline(filepath, new_size)

    def delete(self, key: Union[str, int]) -> None:
        """Deletes an entry by key."""
        _abi.delete(self._handle, self._resolve_hash(key))

    def sync(self) -> None:
        """Flushes archive state to disk."""
        if self._handle is not None:
            _abi.sync(self._handle)

    def close(self) -> None:
        """Closes the underlying C file descriptor handle."""
        if self._handle is not None:
            _abi.close(self._handle)
            self._handle = None

    @staticmethod
    def get_error_code(e: BarrelError) -> Optional[int]:
        """Utility method to extract error code from BarrelError exception."""
        return getattr(e, "code", None)

    def get_header(self) -> BRL_DiskHeader:
        """Reads and parses the BRL_DiskHeader directly from file or live mapping."""
        if not hasattr(self, "filepath") or not self.filepath:
            raise ValueError("Archive filepath is not set.")

        if self._handle is not None:
            self.sync()

        with open(self.filepath, "rb") as f:
            raw_bytes = f.read(ctypes.sizeof(BRL_DiskHeader))
            if len(raw_bytes) < ctypes.sizeof(BRL_DiskHeader):
                raise ValueError("File too small to contain valid BRL_DiskHeader")
            return BRL_DiskHeader.from_buffer_copy(raw_bytes)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
