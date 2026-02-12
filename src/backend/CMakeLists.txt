add_library(cncl_rt_wal_backend_file
    file_backend.cpp
)

target_link_libraries(cncl_rt_wal_backend_file
    PUBLIC
        cncl_rt_wal_core
)
