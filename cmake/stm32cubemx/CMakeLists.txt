# Board support for the existing bare-metal DM-MC-02 firmware.
set(linker_script "${PROJECT_SOURCE_DIR}/STM32H723VGTX_FLASH.ld")
set(cpu_flags -mcpu=cortex-m7 -mthumb -mfloat-abi=soft)

target_sources(firmware PRIVATE
    "${PROJECT_SOURCE_DIR}/Core/Src/board.c"
    "${PROJECT_SOURCE_DIR}/Core/Src/servo_pwm.c"
    "${PROJECT_SOURCE_DIR}/Core/Src/servo_position.c"
    "${PROJECT_SOURCE_DIR}/Core/Src/status_led.c"
    "${PROJECT_SOURCE_DIR}/startup_stm32h723xx.s"
)
target_include_directories(firmware PRIVATE "${PROJECT_SOURCE_DIR}/Core/Inc")
target_compile_options(firmware PRIVATE ${cpu_flags})
target_link_options(firmware PRIVATE
    ${cpu_flags}
    "--ld-path=${STARM_ELF_LINKER}" -nostdlib
    "-T${linker_script}"
    "LINKER:--gc-sections,-z,noexecstack"
    "LINKER:-Map,firmware.map"
)
if(CMAKE_HOST_WIN32)
    # The ST 21.1.1 Windows linker crashes during parallel linking for this firmware.
    target_link_options(firmware PRIVATE "LINKER:--threads=1")
endif()
set_property(TARGET firmware APPEND PROPERTY LINK_DEPENDS "${linker_script}")
