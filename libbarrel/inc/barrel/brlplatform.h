#pragma once
#include "brlcompfix.h"
#include <stdint.h>
#include <stdbool.h>
#include "brlapi.h"

BRL_EXTERN_C_START

#if defined(BRL_WIN) && !defined(BRL_FREESTANDING)
// BRL_WIN

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cfgmgr32.h>
#include <intrin.h>
#include <winioctl.h>

#ifndef DWORD_MAX
#define DWORD_MAX 0xffffffffUL
#endif // DWORD_MAX

#ifndef BRL_MALLOC
#define BRL_MALLOC(size) malloc((size))
#endif // BRL_MALLOC

#ifndef BRL_CALLOC
#define BRL_CALLOC(n, size) calloc(n, size)
#endif // BRL_CALLOC

#ifndef BRL_REALLOC
#define BRL_REALLOC(ptr, size) realloc((ptr), (size))
#endif // BRL_REALLOC

#ifndef BRL_MEMCPY
#define BRL_MEMCPY(dest, src, n) memcpy((dest), (src), (n))
#endif

#ifndef BRL_MEMMOVE
#define BRL_MEMMOVE(dest, src, n) memmove((dest), (src), (n))
#endif // BRL_MEMMOVE

#ifndef BRL_MEMSET
#define BRL_MEMSET(ptr, val, num) memset((ptr), (val), (num))
#endif // BRL_MEMSET

#ifndef BRL_FREE
#define BRL_FREE(ptr) free((ptr))
#endif // BRL_FREE

#ifndef BRL_fd
#define BRL_fd HANDLE
#endif // BRL_fd

#ifndef BRL_IS_INVALID_FD
#define BRL_IS_INVALID_FD(fd) ((fd) == INVALID_HANDLE_VALUE)
#endif // BRL_IS_INVALID_FD

static int __BRL_Win_CLZLL(uint64_t value) {
    unsigned long index;
    _BitScanReverse64(&index, value);
    return 63 - index;
}

#ifndef BRL_CLZLL
#define BRL_CLZLL(n) __BRL_Win_CLZLL((uint64_t)(n))
#endif // BRL_CLZLL

static wchar_t* __BRL_Win_Utf8ToUtf16(const char* str) {
    if (!str) return NULL;

    int wide_char_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0);
    if (wide_char_count == 0) return NULL;

    wchar_t* wide_str = (wchar_t*)BRL_MALLOC(wide_char_count * sizeof(wchar_t));
    if (!wide_str) return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, str, -1, wide_str, wide_char_count) == 0) {
        BRL_FREE(wide_str);
        return NULL;
    }

    return wide_str;
}

static BRL_fd __BRL_Win_FOpen_Create(const char* f) {
    wchar_t* wide_path = __BRL_Win_Utf8ToUtf16(f);
    if (!wide_path) return INVALID_HANDLE_VALUE;

    HANDLE hFile = CreateFileW(
        wide_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    BRL_FREE(wide_path);
    return hFile;
}

#ifndef BRL_FOPEN_CREATE
#define BRL_FOPEN_CREATE(f) __BRL_Win_FOpen_Create((f))
#endif // BRL_FOPEN_CREATE

static BRL_fd __BRL_Win_FOpen(const char* f) {
    wchar_t* wide_path = __BRL_Win_Utf8ToUtf16(f);
    if (!wide_path) return INVALID_HANDLE_VALUE;

    HANDLE hFile = CreateFileW(
        wide_path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    BRL_FREE(wide_path);
    return hFile;
}

#ifndef BRL_FOPEN
#define BRL_FOPEN(f) __BRL_Win_FOpen((f))
#endif // BRL_FOPEN

static inline bool __BRL_Win_Write(
    BRL_fd fd,
    const void* buf,
    uint64_t count,
    uint64_t* out_size
) {
    if (out_size) {
        *out_size = 0;
    }

    if (!buf && count > 0) {
        return false;
    }

    HANDLE hFile = (HANDLE)fd;
    uint64_t total_written = 0;
    const char* ptr = (const char*)buf;

    while (count > 0) {
        DWORD bytes_to_write = (count > DWORD_MAX) ? DWORD_MAX : (DWORD)count;
        DWORD bytes_written = 0;

        BOOL result = WriteFile(
            hFile,
            ptr,
            bytes_to_write,
            &bytes_written,
            NULL
        );

        if (!result) {
            if (out_size) {
                *out_size = total_written;
            }
            return false;
        }

        if (bytes_written == 0) {
            break;
        }

        total_written += bytes_written;
        ptr += bytes_written;
        count -= bytes_written;
    }

    if (out_size) {
        *out_size = total_written;
    }

    return true;
}

#ifndef BRL_FWRITE
// returns bool
#define BRL_FWRITE(fd, buf, size, out_size) __BRL_Win_Write((fd), (buf), (size), (out_size))
#endif // BRL_FWRITE

#ifndef BRL_FCLOSE
#define BRL_FCLOSE(fd) CloseHandle((fd))
#endif // BRL_FCLOSE

static inline bool __BRL_Win_FSize(BRL_fd fd, uint64_t* out_size) {
    if (!out_size) return false;

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(fd, &file_size)) {
        return false;
    }

    *out_size = (uint64_t)file_size.QuadPart;
    return true;
}

#ifndef BRL_FSIZE
#define BRL_FSIZE(fd, out_size) __BRL_Win_FSize((fd), (out_size))
#endif // BRL_FSIZE

static inline bool __BRL_Win_PRead(
    BRL_fd fd,
    void* buf,
    uint64_t count,
    uint64_t offset,
    uint64_t* out_size
) {
    if (!out_size) return false;

    // Prepare OVERLAPPED structure to specify offset without moving file pointer
    OVERLAPPED overlapped = { 0 };
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = (DWORD)(offset >> 32);

    DWORD bytes_read = 0;

    // Perform positional read via ReadFile
    BOOL success = ReadFile(
        fd,
        buf,
        // ReadFile accepts DWORD only
        (DWORD)count,
        &bytes_read,
        &overlapped
    );

    if (!success) {
        // handle ERROR_HANDLE_EOF as 0 bytes 
        // to avoid returning an error
        if (GetLastError() == ERROR_HANDLE_EOF) {
            *out_size = 0;
            return true;
        }

        *out_size = 0;
        return false;
    }

    *out_size = (uint64_t)bytes_read;
    return true;
}

#ifndef BRL_PREAD
#define BRL_PREAD(fd, buf, count, offset, out_size) __BRL_Win_PRead((fd), (buf), (count), (offset), (out_size))
#endif // BRL_PREAD

static inline bool __BRL_Win_PWrite(
    BRL_fd fd,
    const void* buf,
    uint64_t count,
    uint64_t offset,
    uint64_t* out_size
) {
    if (!out_size) return false;

    // Prepare OVERLAPPED structure to specify offset without moving file pointer
    OVERLAPPED overlapped = { 0 };
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = (DWORD)(offset >> 32);

    DWORD bytes_written = 0;

    // Perform positional write via WriteFile
    BOOL success = WriteFile(
        fd,
        buf,
        // WriteFile accepts DWORD only
        (DWORD)count,
        &bytes_written,
        &overlapped
    );

    if (!success) {
        *out_size = 0;
        return false;
    }

    *out_size = (uint64_t)bytes_written;
    return true;
}

#ifndef BRL_PWRITE
#define BRL_PWRITE(fd, buf, count, offset, out_size) __BRL_Win_PWrite((fd), (buf), (count), (offset), (out_size))
#endif // BRL_PWRITE

static inline bool __BRL_Win_FTruncate(BRL_fd fd, uint64_t size) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)size;

    if (!SetFilePointerEx((HANDLE)fd, li, NULL, FILE_BEGIN)) {
        return false;
    }

    return SetEndOfFile((HANDLE)fd) != 0;
}

#ifndef BRL_FTRUNCATE
#define BRL_FTRUNCATE(fd, size) __BRL_Win_FTruncate((fd), (size))
#endif // BRL_FTRUNCATE

// sparse hole creation
static inline bool __BRL_EnsureSparseAlloc(BRL_fd fd, uint64_t req_bytes) {
    uint64_t cur_size = 0;

    if (!BRL_FSIZE(fd, &cur_size)) return false;

    if (cur_size < req_bytes) {
        DWORD bytes_retd = 0;
        BOOL is_sparse_set = DeviceIoControl(
            (HANDLE)fd,
            FSCTL_SET_SPARSE,
            NULL, 0,
            NULL, 0,
            &bytes_retd,
            NULL
        );

        // FSCTL_SET_SPARSE can return false if the filesystem doesn't support sparse files
        // but the file extension will still work as non-sparse.

        // Extend file length without writing physical zero-blocks to disk
        return BRL_FTRUNCATE(fd, req_bytes);
    }

    return true;
}

#ifndef BRL_EnsureSparseAlloc
#define BRL_EnsureSparseAlloc(fd, req_bytes) __BRL_EnsureSparseAlloc((fd), (req_bytes)) 
#endif // BRL_EnsureSparseAlloc

static inline bool __BRL_Win_MMap(
    BRL_fd fd,
    uint64_t size,
    uint64_t offset,
    void** out_ptr
) {
    if (!out_ptr) return false;

    uint64_t total_size = offset + size;

    HANDLE hMap = CreateFileMappingW(
        (HANDLE)fd,
        NULL,
        PAGE_READWRITE,
        (DWORD)(total_size >> 32),
        (DWORD)(total_size & 0xFFFFFFFF),
        NULL
    );

    if (hMap == NULL) {
        *out_ptr = NULL;
        return false;
    }

    void* ptr = MapViewOfFile(
        hMap,
        FILE_MAP_READ | FILE_MAP_WRITE,
        (DWORD)(offset >> 32),
        (DWORD)(offset & 0xFFFFFFFF),
        (SIZE_T)size
    );

    CloseHandle(hMap);

    if (ptr == NULL) {
        *out_ptr = NULL;
        return false;
    }

    *out_ptr = ptr;
    return true;
}

#ifndef BRL_MMAP
#define BRL_MMAP(fd, size, offset, out_ptr) __BRL_Win_MMap((fd), (size), (offset), (out_ptr))
#endif // BRL_MMAP

static inline bool __BRL_Win_MUnmap(
    void* ptr,
    uint64_t size
) {
    (void)size;
    return UnmapViewOfFile(ptr) != 0;
}

#ifndef BRL_MUNMAP
#define BRL_MUNMAP(ptr, size) __BRL_Win_MUnmap((ptr), (size))
#endif // BRL_MUNMAP

static inline bool __BRL_Win_MSync(
    void* ptr,
    uint64_t size,
    int flag
) {
    (void)flag;
    return FlushViewOfFile(ptr, (SIZE_T)size) != 0;
}

#ifndef BRL_MSYNC
#define BRL_MSYNC(ptr, size) __BRL_Win_MSync((ptr), (size), 0)
#endif // BRL_MSYNC

#ifndef BRL_DISABLE_PUNCH_HOLE
static inline bool __BRL_PunchHole(BRL_fd fd, uint64_t offset, uint64_t length) {
    FILE_ZERO_DATA_INFORMATION zero_info;
    zero_info.FileOffset.QuadPart = (LONGLONG)offset;
    zero_info.BeyondFinalZero.QuadPart = (LONGLONG)(offset + length);

    DWORD bytes_returned = 0;
    return DeviceIoControl(
        (HANDLE)fd,
        FSCTL_SET_ZERO_DATA,
        &zero_info,
        sizeof(zero_info),
        NULL,
        0,
        &bytes_returned,
        NULL
    ) != 0;
}
#else
static inline bool __BRL_PunchHole(BRL_fd fd, uint64_t offset, uint64_t length) {
    (void)fd; (void)offset; (void)length;
    return false;
}
#endif // BRL_DISABLE_PUNCH_HOLE

#ifndef BRL_PUNCH_HOLE
#define BRL_PUNCH_HOLE(fd, offset, len) __BRL_PunchHole((fd), (offset), (len))
#endif // BRL_PUNCH_HOLE


#elif defined(BRL_POSIX) && !defined(BRL_FREESTANDING)
    // BRL_POSIX

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

#ifndef BRL_MALLOC
#define BRL_MALLOC(size) malloc((size))
#endif // BRL_MALLOC

#ifndef BRL_CALLOC
#define BRL_CALLOC(n, size) calloc(n, size)
#endif // BRL_CALLOC

#ifndef BRL_REALLOC
#define BRL_REALLOC(ptr, size) realloc((ptr), (size))
#endif // BRL_REALLOC

#ifndef BRL_MEMCPY
#define BRL_MEMCPY(dest, src, n) memcpy((dest), (src), (n))
#endif

#ifndef BRL_MEMMOVE
#define BRL_MEMMOVE(dest, src, n) memmove((dest), (src), (n))
#endif // BRL_MEMMOVE

#ifndef BRL_MEMSET
#define BRL_MEMSET(ptr, val, num) memset((ptr), (val), (num))
#endif // BRL_MEMSET

#ifndef BRL_FREE
#define BRL_FREE(ptr) free((ptr))
#endif // BRL_FREE

#ifndef BRL_fd
#define BRL_fd int
#endif // BRL_fd

#ifndef BRL_IS_INVALID_FD
#define BRL_IS_INVALID_FD(fd) ((fd) < 0)
#endif // BRL_IS_INVALID_FD

#ifndef BRL_CLZLL
#define BRL_CLZLL(n) __builtin_clzll((uint64_t)(n))
#endif // BRL_CLZLL

#ifndef BRL_FOPEN_CREATE
#define BRL_FOPEN_CREATE(f) open((f), O_RDWR | O_CREAT | O_TRUNC, 0644)
#endif // BRL_FOPEN_CREATE

#ifndef BRL_FOPEN
#define BRL_FOPEN(f) open((f), O_RDWR)
#endif

static inline bool __BRL_POSIX_Write(
        BRL_fd fd,
        const void* buf,
        uint64_t count,
        uint64_t* out_size
    ) {
    ssize_t result = write(fd, buf, count);
    if (result < 0) {
        *out_size = 0;
        return false;
    }

    *out_size = (uint64_t)result;
    return true;
}

#ifndef BRL_FWRITE
// returns bool
#define BRL_FWRITE(fd, buf, size, out_size) __BRL_POSIX_Write((fd), (buf), (size), (out_size))
#endif // BRL_FWRITE

#ifndef BRL_FCLOSE
#define BRL_FCLOSE(fd) close((fd))
#endif // BRL_FCLOSE

static inline bool __BRL_POSIX_FSize(BRL_fd fd, uint64_t* out_size) {
    struct stat st;
    if (fstat(fd, &st) != 0)
        return false;

    if (st.st_size < 0)
        return false;

    *out_size = (uint64_t)st.st_size;
    return true;
}

#ifndef BRL_FSIZE
#define BRL_FSIZE(fd, out_size) __BRL_POSIX_FSize((fd), (out_size))
#endif // BRL_FSIZE

static inline bool __BRL_POSIX_PRead(
    BRL_fd fd,
    void* buf,
    uint64_t count,
    uint64_t offset,
    uint64_t* out_size
) {
    ssize_t result = pread(fd, buf, count, (off_t)offset);
    if (result < 0) {
        *out_size = 0;
        return false;
    }

    *out_size = (uint64_t)result;
    return true;
}

#ifndef BRL_PREAD
#define BRL_PREAD(fd, buf, count, offset, out_size) __BRL_POSIX_PRead((fd), (buf), (count), (offset), (out_size))
#endif // BRL_PREAD

static inline bool __BRL_POSIX_PWrite(
    BRL_fd fd,
    const void* buf,
    uint64_t count,
    uint64_t offset,
    uint64_t* out_size
) {
    ssize_t result = pwrite(fd, buf, count, (off_t)offset);
    if (result < 0) {
        *out_size = 0;
        return false;
    }

    *out_size = (uint64_t)result;
    return true;
}

#ifndef BRL_PWRITE
#define BRL_PWRITE(fd, buf, count, offset, out_size) __BRL_POSIX_PWrite((fd), (buf), (count), (offset), (out_size))
#endif // BRL_PWRITE

static inline bool __BRL_POSIX_FTruncate(BRL_fd fd, uint64_t size) {
    return ftruncate(fd, (off_t)size) == 0;
}

#ifndef BRL_FTRUNCATE
#define BRL_FTRUNCATE(fd, size) __BRL_POSIX_FTruncate((fd), (size))
#endif // BRL_FTRUNCATE

// sparse hole creation
static inline bool __BRL_EnsureSparseAlloc(BRL_fd fd, uint64_t req_bytes) {
    uint64_t cur_size = 0;
    if (!BRL_FSIZE(fd, &cur_size)) return false;
    if (cur_size < req_bytes) {
        return BRL_FTRUNCATE(fd, req_bytes);
    }
    return true;
}

#ifndef BRL_EnsureSparseAlloc
#define BRL_EnsureSparseAlloc(fd, req_bytes) __BRL_EnsureSparseAlloc((fd), (req_bytes)) 
#endif // BRL_EnsureSparseAlloc

static inline bool __BRL_POSIX_MMap(
    BRL_fd fd,
    uint64_t size,
    uint64_t offset,
    void** out_ptr
) {
    void* ptr = mmap(
        NULL,
        (size_t)size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        (off_t)offset
    );

    if (ptr == MAP_FAILED) {
        *out_ptr = NULL;
        return false;
    }

    *out_ptr = ptr;
    return true;
}

#ifndef BRL_MMAP
#define BRL_MMAP(fd, size, offset, out_ptr) __BRL_POSIX_MMap((fd), (size), (offset), (out_ptr))
#endif // BRL_MMAP

static inline bool __BRL_POSIX_MUnmap(
    void* ptr,
    uint64_t size
) {
    return munmap(ptr, (size_t)size) == 0;
}

#ifndef BRL_MUNMAP
#define BRL_MUNMAP(ptr, size) __BRL_POSIX_MUnmap((ptr), (size))
#endif // BRL_MUNMAP

static inline bool __BRL_POSIX_MSync(
    void* ptr,
    uint64_t size,
    int flag
) {
    return msync(ptr, (size_t)size, flag) == 0;
}

#ifndef BRL_MSYNC
#define BRL_MSYNC(ptr, size) __BRL_POSIX_MSync((ptr), (size), MS_SYNC)
#endif // BRL_MSYNC

// #ifndef BRL_MASYNC
// #define BRL_MASYNC(ptr, size) __BRL_POSIX_MSync((ptr), (size), MS_ASYNC)
// #endif // BRL_MASYNC

#ifndef BRL_DISABLE_PUNCH_HOLE

#if defined(BRL_MACOS)
// BRL_MACOS
static inline bool __BRL_PunchHole(BRL_fd fd, uint64_t offset, uint64_t length) {
    fpunchhole_t fp = {
        .fp_flags = 0,
        .fp_offset = (off_t)offset,
        .fp_length = (off_t)length
    };
    return fcntl(fd, F_PUNCHHOLE, &fp) == 0;
}
#elif defined(BRL_LINUX)
// BRL_LINUX
static inline bool __BRL_PunchHole(BRL_fd fd, uint64_t offset, uint64_t length) {
    return fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)offset, (off_t)length) == 0;
}
#else
static inline bool __BRL_PunchHole(BRL_fd fd, uint64_t offset, uint64_t length) {
    (void)fd; (void)offset; (void)length;
    return false;
}
#endif // BRL_MACOS || BRL_LINUX

#else 
static inline bool __BRL_PunchHole(BRL_fd fd, uint64_t offset, uint64_t length) {
    (void)fd; (void)offset; (void)length;
    return false;
}
#endif // BRL_DISABLE_PUNCH_HOLE

#ifndef BRL_PUNCH_HOLE
#define BRL_PUNCH_HOLE(fd, offset, len) __BRL_PunchHole((fd), (offset), (len))
#endif // BRL_PUNCH_HOLE

#endif

#ifndef BRL_MALLOC
#error "BRL_MALLOC undefined"
#endif // BRL_MALLOC

#ifndef BRL_CALLOC
#error "BRL_CALLOC undefined"
#endif // BRL_CALLOC

#ifndef BRL_REALLOC
#error "BRL_REALLOC undefined"
#endif // BRL_REALLOC

#ifndef BRL_MEMCPY
#error "BRL_MEMCPY undefined"
#endif

#ifndef BRL_MEMMOVE
#error "BRL_MEMMOVE undefined"
#endif // BRL_MEMMOVE

#ifndef BRL_MEMSET
#error "BRL_MEMSET undefined"
#endif // BRL_MEMSET

#ifndef BRL_FREE
#error "BRL_FREE undefined"
#endif // BRL_FREE

#ifndef BRL_fd
#error "BRL_fd undefined"
#endif // BRL_fd

#ifndef BRL_IS_INVALID_FD
#error "BRL_IS_INVALID_FD undefined"
#endif // BRL_IS_INVALID_FD

#ifndef BRL_CLZLL
#error "BRL_CLZLL undefined"
#endif // BRL_CLZLL

#ifndef BRL_FOPEN_CREATE
#error "BRL_FOPEN_CREATE undefined"
#endif // BRL_FOPEN_CREATE

#ifndef BRL_FOPEN
#error "BRL_FOPEN undefined"
#endif

#ifndef BRL_FWRITE
#error "BRL_FWRITE undefined"
#endif // BRL_FWRITE

#ifndef BRL_FCLOSE
#error "BRL_FCLOSE undefined"
#endif // BRL_FCLOSE

#ifndef BRL_FSIZE
#error "BRL_FSIZE undefined"
#endif // BRL_FSIZE

#ifndef BRL_PREAD
#error "BRL_PREAD undefined"
#endif // BRL_PREAD

#ifndef BRL_FTRUNCATE
#error "BRL_FTRUNCATE undefined"
#endif // BRL_FTRUNCATE

#ifndef BRL_EnsureSparseAlloc
#error "BRL_EnsureSparseAlloc undefined"
#endif // BRL_EnsureSparseAlloc

#ifndef BRL_MMAP
#error "BRL_MMAP undefined"
#endif // BRL_MMAP

#ifndef BRL_MUNMAP
#error "BRL_MUNMAP undefined"
#endif // BRL_MUNMAP

#ifndef BRL_MSYNC
#error "BRL_MSYNC undefined"
#endif // BRL_MSYNC

#ifndef BRL_PUNCH_HOLE
#error "BRL_PUNCH_HOLE undefined"
#endif // BRL_PUNCH_HOLE

BRL_EXTERN_C_END
