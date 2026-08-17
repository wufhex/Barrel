file(GLOB_RECURSE CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

# Locate libs
find_package(PkgConfig REQUIRED)

pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)
if(NOT LZ4_FOUND)
    message(FATAL_ERROR "LZ4 was not found!")
endif()

add_executable(barrel-ts ${CLI_SOURCES})

# Target include directories
target_include_directories(barrel-ts
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Link libs
target_link_libraries(barrel-ts
    PRIVATE
        barrel
        PkgConfig::LZ4
)

# Compile definitions
target_compile_definitions(barrel-ts
    PRIVATE
        BRL_ENV_CC
)

# Copy dependent DLLs to the executable directory on Windows
if(WIN32)
    add_custom_command(TARGET barrel-ts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:barrel>
            $<TARGET_FILE_DIR:barrel-ts>
        COMMAND_EXPAND_LISTS
    )

    # Copy LZ4 DLL if it was built dynamically
    if(TARGET PkgConfig::LZ4)
        add_custom_command(TARGET barrel-ts POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:barrel-ts>
                $<TARGET_FILE_DIR:barrel-ts>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
