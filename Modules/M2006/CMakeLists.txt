target_sources(firmware PRIVATE
    m2006_control.c
    m2006_position.c
    m2006_remote.c
    m2006_speed_control.c
)
target_include_directories(firmware PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
