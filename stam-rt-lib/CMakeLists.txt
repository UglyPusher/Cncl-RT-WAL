# stam-rt-lib
# RT framework: domain model, execution layer, and runtime stubs (rtr).
# Depends on stam_primitives for the sys/ portability layer and data channels.

option(BUILD_RTR "Build real-time runtime layer (stam_rtr)" ON)

# ---- Model -----------------------------------------------------------------
# Domain concepts and tags: RtSafe, Steppable, RtPayload.
# No dependencies beyond the C++ standard library.

add_library(stam_model INTERFACE)
target_include_directories(stam_model INTERFACE include)
target_compile_features(stam_model INTERFACE cxx_std_20)

# ---- Exec ------------------------------------------------------------------
# Execution layer: task wrappers and scheduling primitives.
# Inherits sys/ and data channels transitively from stam_primitives.

add_library(stam_exec INTERFACE)
target_link_libraries(stam_exec INTERFACE stam_model stam_primitives)

# ---- RTR -------------------------------------------------------------------

if(BUILD_RTR)
    add_subdirectory(rtr)
endif()

# ---- Tests -----------------------------------------------------------------

option(BUILD_EXEC_TESTS "Build exec layer tests (stam_exec_tests)" ON)

if(BUILD_EXEC_TESTS)
    add_subdirectory(tests/exec)
endif()
