file(GLOB_RECURSE LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h"
)

# Set library type based on flag
if(BRL_BUILD_SHARED)
    set(LIB_TYPE SHARED)
else()
    set(LIB_TYPE STATIC)
endif()

add_library(libbarrel ${LIB_TYPE} ${LIB_SOURCES})

# Include 'inc/' for public library headers, and 'src/' for internal headers
target_include_directories(libbarrel
    PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
