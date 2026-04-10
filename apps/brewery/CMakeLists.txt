add_executable(app_brewery)

target_sources(app_brewery
    PRIVATE
        main.cpp
)

target_link_libraries(app_brewery
    PRIVATE
        stam_exec
        stam_rtr
        module_logging
)
    