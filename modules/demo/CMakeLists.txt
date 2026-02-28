add_library(demo_trivial_tasks STATIC)

target_sources(demo_trivial_tasks
    PRIVATE
        src/trivial_rt_task.cpp
        src/trivial_nonrt_task.cpp
)

target_include_directories(demo_trivial_tasks
    PUBLIC
        include
)

target_link_libraries(demo_trivial_tasks
    PUBLIC
        stam_exec
)
