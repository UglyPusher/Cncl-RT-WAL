add_library(module_logging STATIC)

target_sources(module_logging
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/modules/logging/backend/file_backend.cpp
        ${CMAKE_SOURCE_DIR}/src/modules/logging/writer/writer.cpp
)

target_include_directories(module_logging
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(module_logging
    PUBLIC
        core_exec          # logging использует exec-публикации
)
