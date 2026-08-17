from typing import Tuple, Optional, Callable, Dict, Any
import _barrel

__BRL_PY_VERSION__ = "1.0.0"

for _attr in dir(_barrel):
    if _attr.startswith("BRL_"):
        globals()[_attr] = getattr(_barrel, _attr)

class Archive:
    def __init__(self, native_obj: Any):
        self._native = native_obj

    def close(self) -> int:
        """Flushes pending changes to disk and unmaps the archive."""
        if self._native is not None:
            err = _barrel.close(self._native)
            self._native = None
            return err
        return getattr(_barrel, "BRL_OK", 0)

    def __enter__(self) -> "Archive":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def read(self, hash_val: int) -> Tuple[int, Optional[memoryview]]:
        """
        Reads entry by hash key.
        Returns: (error_code, memoryview_or_None)
        """
        return _barrel.read(self._native, hash_val)

    def read_copy(self, hash_val: int, dst_capacity: int) -> Tuple[int, Optional[bytes]]:
        """
        Reads entry into a freshly allocated bytes buffer.
        Returns: (error_code, bytes_or_None)
        """
        return _barrel.read_copy(self._native, hash_val, dst_capacity)

    def write(self, hash_val: int, data: bytes) -> int:
        """Writes data into an archive slot by hash key."""
        return _barrel.write(self._native, hash_val, data)

    def write_ex(self, hash_val: int, data: bytes, use_compressor: bool) -> int:
        """Writes data into an archive slot with optional compression."""
        return _barrel.write_ex(self._native, hash_val, data, use_compressor)

    def delete(self, hash_val: int) -> int:
        """Deletes an entry slot by hash key."""
        return _barrel.delete(self._native, hash_val)

    def sync(self) -> int:
        """Synchronizes memory-mapped regions to disk storage."""
        return _barrel.sync(self._native)

    def register_compressor(
        self,
        compress_fn: Callable[[bytes, int], bytes],
        decompress_fn: Callable[[bytes, int, int], bytes],
        bound_fn: Callable[[int], int],
    ) -> None:
        """
        Registers Python custom compression, decompression, and boundary callbacks.
        """
        return _barrel.register_compressor(
            self._native, compress_fn, decompress_fn, bound_fn
        )

    def get_header(self) -> Dict[str, Any]:
        """Retrieves raw archive header fields as a dictionary."""
        return _barrel.get_header(self._native)

    def get_entry_meta(self, slot_idx: int) -> Dict[str, Any]:
        """Retrieves metadata fields for a specific slot index as a dictionary."""
        return _barrel.get_entry_meta(self._native, slot_idx)

def create(
    filepath: str,
    hints: int = 0,
    initial_index_capacity: int = 256,
    max_virtual_capacity: int = 1 << 30,
) -> int:
    """Creates a new empty Barrel file on disk."""
    return _barrel.create(
        filepath, hints, initial_index_capacity, max_virtual_capacity
    )

def open(filepath: str, open_flags: int = 0) -> Tuple[int, Optional[Archive]]:
    """
    Opens an existing Barrel archive file.
    Returns: (error_code, Archive_instance_or_None)
    """
    err, native_obj = _barrel.open(filepath, open_flags)
    brl_ok = getattr(_barrel, "BRL_OK", 0)
    
    if err == brl_ok and native_obj is not None:
        return err, Archive(native_obj)
    return err, None

def hash_string(name: str) -> int:
    """Computes standard hash integer for string key identifiers."""
    return _barrel.hash_string(name)

def format_error(err_code: int) -> str:
    """Formats an integer error code into a human-readable string."""
    return _barrel.format_error(err_code)

__all__ = [
    # Classes
    "Archive",
    # Functions
    "create",
    "open",
    "hash_string",
    "format_error",
]
