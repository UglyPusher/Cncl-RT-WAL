add_executable(app_brewery)

target_sources(app_brewery
    PRIVATE
        main.cpp
)

target_link_libraries(app_brewery
    PRIVATE
        core_exec
        core_rtr        # можно выключить опцией BUILD_RTR
        module_logging  # опционально
)
