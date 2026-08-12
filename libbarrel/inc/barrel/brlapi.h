#pragma once
#include "brlcompfix.h"

#if defined(_WIN32)
    #if defined(__TINYC__)
        #define __declspec(x) __attribute__((x))
    #endif // __TINYC__
    #if defined(BUILD_LIBTYPE_SHARED)
        #define BRLAPI __declspec(dllexport)
    #elif defined(USE_LIBTYPE_SHARED)
        #define BRLAPI __declspec(dllimport)
    #endif // BUILD_LIBTYPE_SHARED
#else
    #if defined(BUILD_LIBTYPE_SHARED)
        #define BRLAPI __attribute__((visibility("default")))
    #endif // BUILD_LIBTYPE_SHARED
#endif // _WIN32

#ifndef BRLAPI
    #define BRLAPI
#endif // BRLAPI

#ifdef __cplusplus
    #define BRL_EXTERN_C_START extern "C" {
#else
    #define BRL_EXTERN_C_START
#endif

#ifdef __cplusplus
    #define BRL_EXTERN_C_END }
#else
    #define BRL_EXTERN_C_END
#endif

#ifdef __cplusplus 
    #ifndef BRL_ENV_CC
    #define BRL_ENV_CC
    #endif // BRL_ENV_CC
#else 
    #ifndef BRL_ENV_C
    #define BRL_ENV_C
    #endif // BRL_ENV_C
#endif 