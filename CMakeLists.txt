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

# Standards Setup
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add libbarrel 
add_subdirectory(libbarrel)

# Add consumers
if(BRL_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()

if(BRL_BUILD_TS)
    add_subdirectory(test_suite)
endif()
