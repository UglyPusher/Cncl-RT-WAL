add_executable(app_trv_task)

target_sources(app_trv_task
    PRIVATE
        main.cpp
        sys_preemption_stub.cpp
)

target_link_libraries(app_trv_task
    PRIVATE
        stam_exec
        demo_trivial_tasks
        stam_rtr
)
