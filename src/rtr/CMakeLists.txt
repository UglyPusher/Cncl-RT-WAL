add_library(core_rtr STATIC)

target_sources(core_rtr
    PRIVATE
        dummy.cpp
        # добавляй .cpp по мере появления
)

target_include_directories(core_rtr
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(core_rtr
    PUBLIC
        core_exec
)
