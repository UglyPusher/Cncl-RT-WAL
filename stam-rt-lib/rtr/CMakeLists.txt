add_library(stam_rtr STATIC)

target_sources(stam_rtr
    PRIVATE
        dummy.cpp
        # add .cpp files as the runtime layer grows
)

target_link_libraries(stam_rtr
    PUBLIC
        stam_exec
)
