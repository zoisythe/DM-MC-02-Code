set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(stm32_bundles "$ENV{CUBE_BUNDLE_PATH}")
if(NOT stm32_bundles AND CMAKE_HOST_WIN32)
    set(stm32_bundles "$ENV{LOCALAPPDATA}/stm32cube/bundles")
endif()

set(stm32_clang_bins)
if(stm32_bundles)
    file(GLOB stm32_clang_bins LIST_DIRECTORIES true "${stm32_bundles}/st-arm-clang/*/bin")
    list(SORT stm32_clang_bins COMPARE NATURAL ORDER DESCENDING)
endif()

find_program(STARM_CLANG starm-clang HINTS ${stm32_clang_bins})
if(NOT STARM_CLANG)
    message(FATAL_ERROR "Install the st-arm-clang STM32Cube bundle or put starm-clang on PATH.")
endif()
get_filename_component(clang_bin "${STARM_CLANG}" DIRECTORY)

set(CMAKE_C_COMPILER "${STARM_CLANG}")
set(CMAKE_ASM_COMPILER "${STARM_CLANG}")
set(CMAKE_C_COMPILER_TARGET arm-st-none-eabi)
set(CMAKE_ASM_COMPILER_TARGET arm-st-none-eabi)

find_program(STARM_ELF_LINKER ld.lld HINTS "${clang_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(STARM_OBJCOPY starm-objcopy HINTS "${clang_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(STARM_SIZE starm-size HINTS "${clang_bin}" NO_DEFAULT_PATH REQUIRED)
