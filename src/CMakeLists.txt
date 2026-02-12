# ================================================================
# 1. RT primitives (header-only)
# ================================================================

add_library(cncl_rt_primitives INTERFACE)

target_include_directories(cncl_rt_primitives
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(cncl_rt_primitives
    INTERFACE
        cxx_std_20
)

# ================================================================
# 2. WAL Core (writer + dispatcher)
# ================================================================

add_subdirectory(writer)

# ================================================================
# 3. Backends (optional)
# ================================================================

if (CNCL_RT_WAL_BUILD_FILE_BACKEND)
    add_subdirectory(backend)
endif()
