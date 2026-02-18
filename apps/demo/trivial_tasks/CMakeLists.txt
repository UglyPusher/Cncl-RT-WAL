add_executable(app_trv_task)

target_sources(app_trv_task
    PRIVATE
        main.cpp
)

target_link_libraries(app_trv_task
    PRIVATE
        core_exec
        demo_trivial_tasks
        core_rtr        # можно выключить опцией BUILD_RTR
)
