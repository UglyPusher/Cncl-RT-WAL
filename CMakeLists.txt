cmake_minimum_required(VERSION 3.20)

project(CnclRTWAL LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ---- Options ---------------------------------------------------------------

option(BUILD_RTR        "Build real-time runtime layer" ON)
option(BUILD_MODULES    "Build optional modules" ON)
option(BUILD_APPS       "Build example applications" ON)
option(BUILD_TESTS      "Build tests" ON)

# ---- Include root ----------------------------------------------------------

add_library(core_sys INTERFACE)
target_include_directories(core_sys INTERFACE include)

add_library(core_model INTERFACE)
target_link_libraries(core_model INTERFACE core_sys)

add_library(core_exec INTERFACE)
target_link_libraries(core_exec INTERFACE core_model)

# ---- RTR -------------------------------------------------------------------

if(BUILD_RTR)
    add_subdirectory(src/rtr)
endif()

# ---- Optional modules ------------------------------------------------------

if(BUILD_MODULES)
    add_subdirectory(modules)
endif()

# ---- Apps ------------------------------------------------------------------

if(BUILD_APPS)
    add_subdirectory(apps)
endif()

# ---- Tests -----------------------------------------------------------------

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
