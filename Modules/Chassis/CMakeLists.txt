target_sources(firmware PRIVATE
    chassis.c
    chassis_control.c
    control_state.c
    chassis_speed_control.c
)
target_include_directories(firmware PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
