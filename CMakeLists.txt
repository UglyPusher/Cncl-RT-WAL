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

# ---- Diagrams --------------------------------------------------------------

find_program(CLANG_UML_EXE clang-uml)

if(CLANG_UML_EXE)
    add_custom_target(diagrams_primitives
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/docs/diagrams
        COMMAND ${CLANG_UML_EXE} -c ${CMAKE_SOURCE_DIR}/clang-uml.yaml -n primitives_class
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/plantuml_layout.sh
                ${CMAKE_SOURCE_DIR}/docs/diagrams/primitives_class.puml
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating UML diagrams (primitives)"
        VERBATIM
    )
else()
    add_custom_target(diagrams_primitives
        COMMAND ${CMAKE_COMMAND} -E echo
                "clang-uml not found. Install it and re-run CMake."
    )
endif()
