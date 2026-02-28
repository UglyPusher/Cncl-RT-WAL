add_library(module_logging STATIC)

target_sources(module_logging
    PRIVATE
        src/logger_task.cpp
        src/backend/file_backend.cpp
        src/writer/writer.cpp
)

target_include_directories(module_logging
    PRIVATE
        src
)

target_link_libraries(module_logging
    PUBLIC
        stam_exec
)
