target_sources(firmware PRIVATE remote_uart.c sbus.c)
target_include_directories(firmware PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
