add_executable(wal_smoke
    dispatcher_smoke.cpp
)

target_link_libraries(wal_smoke
    PRIVATE
        cncl_rt_wal
)

add_test(NAME wal_smoke COMMAND wal_smoke)
