cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pull in SDK (must be before project)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(chip8 C CXX ASM)

# Initialize the SDK
pico_sdk_init()

add_subdirectory(vga_lib)

add_executable(chip8)

# must match with executable name and source file names
target_sources(chip8 PRIVATE chip8.c)

# must match with executable name
target_link_libraries(chip8 PRIVATE pico_stdlib hardware_pio hardware_dma hardware_irq vga_lib)

# must match with executable name
pico_add_extra_outputs(chip8)
