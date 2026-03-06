cmake_minimum_required(VERSION 3.20)

project(CnclRTWAL LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Options ---------------------------------------------------------------

option(BUILD_MODULES    "Build optional modules" ON)
option(BUILD_APPS       "Build example applications" ON)
option(BUILD_TESTS      "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
endif()

# ---- Primitives ------------------------------------------------------------

add_subdirectory(primitives)

# ---- RT Framework ----------------------------------------------------------

add_subdirectory(stam-rt-lib)

# ---- Optional modules ------------------------------------------------------

if(BUILD_MODULES)
    add_subdirectory(modules)
endif()

# ---- Apps ------------------------------------------------------------------

if(BUILD_APPS)
    add_subdirectory(apps)
endif()
