file(GLOB_RECURSE LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h"
)

if(BRL_BUILD_SHARED)
    set(LIB_TYPE SHARED)
else()
    set(LIB_TYPE STATIC)
endif()

add_library(barrel ${LIB_TYPE} ${LIB_SOURCES})

# Include Directories
target_include_directories(barrel
    PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Shared/Static Definitions
if(BRL_BUILD_SHARED)
    target_compile_definitions(barrel 
        PRIVATE BUILD_LIBTYPE_SHARED
        INTERFACE USE_LIBTYPE_SHARED
    )
endif()

# Build Configuration Flags
target_compile_definitions(barrel PUBLIC
    $<$<CONFIG:Debug>:BRL_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:BRL_RELEASE>
)

# OS/Platform Detection Flags
if(WIN32)
    target_compile_definitions(barrel PUBLIC BRL_WIN)
elseif(APPLE)
    target_compile_definitions(barrel PUBLIC BRL_MACOS BRL_POSIX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(barrel PUBLIC BRL_LINUX BRL_POSIX)
elseif(UNIX)
    target_compile_definitions(barrel PUBLIC BRL_POSIX)
endif()
