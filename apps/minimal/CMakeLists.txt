add_executable(app_minimal)

target_sources(app_minimal
    PRIVATE
        main.cpp
)

target_link_libraries(app_minimal
    PRIVATE
        stam_exec
        stam_rtr
        module_logging
)
