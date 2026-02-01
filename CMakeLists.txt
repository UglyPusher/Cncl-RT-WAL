cmake_minimum_required(VERSION 3.22)

project(cncl_rt_wal
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------
# Global configuration
# ---------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------

add_library(cncl_rt_wal
    src/wal.cpp
    src/record.cpp
    src/backend/file_backend.cpp
)

target_include_directories(cncl_rt_wal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------
# Compile options (НЕ RT-финальные, но безопасные)
# ---------------------------------------------------------------------

target_compile_options(cncl_rt_wal
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# ---------------------------------------------------------------------
# Optional: tests
# ---------------------------------------------------------------------

option(CNCL_RT_WAL_BUILD_TESTS "Build tests" OFF)

if (CNCL_RT_WAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
