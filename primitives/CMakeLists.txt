cmake_minimum_required(VERSION 3.20)

project(stam_primitives LANGUAGES CXX)

# --------------------------------------------------
# Library: stam (header-only)
# --------------------------------------------------

add_library(stam_primitives INTERFACE)

target_include_directories(stam_primitives
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(stam_primitives
    INTERFACE
        cxx_std_20
)

# Optional strict compile flags

if (MSVC)

    target_compile_options(stam_primitives INTERFACE
        /permissive-
        /W4
        /utf-8
    )

else()

    target_compile_options(stam_primitives INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )

endif()


# --------------------------------------------------
# Tests
# --------------------------------------------------

option(STAM_BUILD_TESTS "Build stam tests" ON)

if(STAM_BUILD_TESTS)
    add_subdirectory(tests)
endif()