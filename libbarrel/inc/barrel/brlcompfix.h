#pragma once

#ifndef BRL_DISABLE_COMPILER_FIXES

// Enable support for pread() everywhere if compiler doesnt define it
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif // _POSIX_C_SOURCE

#ifndef BRL_DISABLE_PUNCH_HOLE

// Enable support for fallocate()
#ifdef BRL_MACOS
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif // _DARWIN_C_SOURCE
#endif // BRL_MACOS

#ifdef BRL_LINUX
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // _GNU_SOURCE
#endif // BRL_LINUX

#endif // BRL_DISABLE_PUNCH_HOLE

// Static assert for C99+
#ifndef BRL_DISABLE_STATIC_LEN_CHECKS

#ifndef BRL_STATIC_ASSERT
#define BRL_STATIC_ASSERT_CAT_(a, b) a##b
#define BRL_STATIC_ASSERT_CAT(a, b)  BRL_STATIC_ASSERT_CAT_(a, b)
#define BRL_STATIC_ASSERT(expr, msg) \
    typedef char BRL_STATIC_ASSERT_CAT(brl_static_assert_, __LINE__)[(expr) ? 1 : -1]
#endif // BRL_STATIC_ASSERT

#endif // BRL_DISABLE_STATIC_LEN_CHECKS

#endif // BRL_DISABLE_COMPILER_FIXES

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #error "BRL Archive relies on Little-Endian memory layout for direct memory mapping."
#endif // __ORDER_BIG_ENDIAN__
