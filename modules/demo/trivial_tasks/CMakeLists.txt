add_library(demo_trivial_tasks STATIC)

target_sources(demo_trivial_tasks
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/modules/demo/trivial_rt_task.cpp
        ${CMAKE_SOURCE_DIR}/src/modules/demo/trivial_nonrt_task.cpp
)

target_include_directories(demo_trivial_tasks
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(demo_trivial_tasks
    PUBLIC
        core_exec          # logging использует exec-публикации
)
