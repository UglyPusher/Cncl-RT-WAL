add_executable(app_minimal)

target_sources(app_minimal
    PRIVATE
        main.cpp
)

target_link_libraries(app_minimal
    PRIVATE
        core_exec
        core_rtr        # можно выключить опцией BUILD_RTR
        module_logging  # опционально
)
