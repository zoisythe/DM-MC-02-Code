target_sources(firmware PRIVATE actuator_control.c)
target_include_directories(firmware PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
