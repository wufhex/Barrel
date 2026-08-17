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

if(BRL_BUILD_SHARED)
    target_compile_definitions(barrel 
        PRIVATE BUILD_LIBTYPE_SHARED
        INTERFACE USE_LIBTYPE_SHARED
    )
endif()

target_include_directories(barrel
    PUBLIC 
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
