#pragma once
#include "brlcompfix.h"
#include <stdint.h>
#include <stdbool.h>
#include "brlapi.h"

BRL_EXTERN_C_START

#if defined(BRL_WIN) && !defined(BRL_FREESTANDING)
// BRL_WIN

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
#define BRL_FCLOSE(fd) close(fd)
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
static inline bool __BRL_PunchHole(int fd, uint64_t offset, uint64_t length) {
    fpunchhole_t fp = {
        .fp_flags = 0,
        .fp_offset = (off_t)offset,
        .fp_length = (off_t)length
    };
    return fcntl(fd, F_PUNCHHOLE, &fp) == 0;
}
#elif defined(BRL_LINUX)
// BRL_LINUX
static inline bool __BRL_PunchHole(int fd, uint64_t offset, uint64_t length) {
    return fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, length) == 0;
}
#endif // BRL_MACOS || BRL_LINUX

#else 
static inline bool __BRL_PunchHole(int fd, uint64_t offset, uint64_t length) {
    (void)fd; (void)offset; (void)length;
    return false;
}
#endif // BRL_DISABLE_PUNCH_HOLE

#ifndef BRL_PUNCH_HOLE
#define BRL_PUNCH_HOLE(fd, offset, len) __BRL_PunchHole((fd), (offset), (len))
#endif // BRL_PUNCH_HOLE

#else 
// FREESTANDING

#endif

BRL_EXTERN_C_END
