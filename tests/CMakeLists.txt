add_executable(dispatcher_smoke
    dispatcher_smoke.cpp
)

target_link_libraries(dispatcher_smoke
    PRIVATE
        cncl_rt_wal_core
)

add_test(NAME dispatcher_smoke COMMAND dispatcher_smoke)
