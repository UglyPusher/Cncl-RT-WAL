add_library(cncl_rt_wal_core
    writer.cpp
    ../writers_dispatcher.cpp
)

target_link_libraries(cncl_rt_wal_core
    PUBLIC
        cncl_rt_primitives
)

target_compile_options(cncl_rt_wal_core
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
