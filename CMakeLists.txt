cmake_minimum_required(VERSION 3.23)

# Use the native Ninja executable when STM32Cube provides one on Windows.
if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "^Ninja" AND NOT DEFINED CMAKE_MAKE_PROGRAM)
    file(GLOB ninja_bundles "$ENV{LOCALAPPDATA}/stm32cube/bundles/ninja/*/bin/ninja.exe")
    if(ninja_bundles)
        list(SORT ninja_bundles COMPARE NATURAL ORDER DESCENDING)
        list(GET ninja_bundles 0 bundled_ninja)
        set(CMAKE_MAKE_PROGRAM "${bundled_ninja}" CACHE FILEPATH "Ninja executable")
    endif()
endif()

project(DM_MC02 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT STARM_ELF_LINKER)
    message(FATAL_ERROR "Use the Debug or Release preset, or select cmake/starm-clang.cmake.")
endif()
enable_language(ASM)

option(GRIPPER_BENCH "Build the ST-Link controlled gripper test firmware" OFF)

add_executable(firmware)
set_target_properties(firmware PROPERTIES SUFFIX ".elf")
target_compile_options(firmware PRIVATE
    $<$<CONFIG:Debug>:-g3>
    $<$<COMPILE_LANGUAGE:C>:-Os;-ffunction-sections;-fdata-sections;-Wall;-Wextra;-Wconversion;-Werror;-ffreestanding;-fno-builtin;-fno-common>
    $<$<COMPILE_LANGUAGE:ASM>:-Wno-unused-command-line-argument>
)

add_subdirectory(cmake/stm32cubemx)
if(GRIPPER_BENCH)
    target_sources(firmware PRIVATE Modules/Gripper/gripper_bench.c)
else()
    target_sources(firmware PRIVATE Core/Src/main.c)
    add_subdirectory(Modules/Chassis)
    add_subdirectory(Modules/Gripper)
    add_subdirectory(Modules/M2006)
    add_subdirectory(Modules/Remote)
    add_subdirectory(Modules/Slide)
endif()

add_custom_command(
    OUTPUT firmware.hex firmware.bin
    COMMAND "${STARM_OBJCOPY}" -O ihex "$<TARGET_FILE:firmware>" firmware.hex
    COMMAND "${STARM_OBJCOPY}" -O binary "$<TARGET_FILE:firmware>" firmware.bin
    COMMAND "${STARM_SIZE}" "$<TARGET_FILE:firmware>"
    DEPENDS firmware
    VERBATIM
)
add_custom_target(firmware_images ALL DEPENDS firmware.hex firmware.bin)
