cmake_minimum_required(VERSION 3.25)
project(
    barrel 
    VERSION 1.0.0 
    LANGUAGES C CXX
)

# Options
option(BRL_BUILD_SHARED  "Build libbarrel as a shared library" OFF)
option(BRL_BUILD_EXAMPLE "Build barrel example"                OFF)
option(BRL_BUILD_TS      "Build barrel test suite"             OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Force C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compile Definitions

# Build Configuration Flags 
add_compile_definitions(
    $<$<CONFIG:Debug>:BRL_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:BRL_RELEASE>
)

# OS/Platform Detection Flags
if(WIN32)
    add_compile_definitions(BRL_WIN)
elseif(APPLE)
    # macOS is both BRL_MACOS and BRL_POSIX
    add_compile_definitions(BRL_MACOS BRL_POSIX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux is both BRL_LINUX and BRL_POSIX
    add_compile_definitions(BRL_LINUX BRL_POSIX)
elseif(UNIX)
    # Catch-all for other Unix-like OSes (BSD, Solaris, etc.)
    add_compile_definitions(BRL_POSIX)
endif()

# Subdirectories
add_subdirectory(libbarrel)

if(BRL_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()

if(BRL_BUILD_TS)
    add_subdirectory(test_suite)
endif()
