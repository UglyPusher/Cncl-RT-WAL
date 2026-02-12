cmake_minimum_required(VERSION 3.22)

project(cncl_rt_wal
    VERSION 0.1.0
    LANGUAGES CXX
)

# ------------------------------------------------------------------
# Global configuration
# ------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ------------------------------------------------------------------
# Options
# ------------------------------------------------------------------

option(CNCL_RT_WAL_BUILD_TESTS "Build unit tests" OFF)
option(CNCL_RT_WAL_BUILD_FILE_BACKEND "Build file backend" ON)

# ------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------

add_subdirectory(src)

if (CNCL_RT_WAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
