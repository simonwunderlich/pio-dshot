cmake_minimum_required(VERSION 3.13)

include(../pico_sdk_import.cmake)

project(pio_dshot_examples CXX C ASM)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(example1 example1.c)
target_link_libraries(example1 pico_stdlib hardware_pio dshot)
pico_enable_stdio_usb(example1 1)
pico_enable_stdio_uart(example1 0)
pico_add_extra_outputs(example1)

add_executable(example2 example2.c)
target_link_libraries(example2 pico_stdlib hardware_pio dshot)
pico_enable_stdio_usb(example2 1)
pico_enable_stdio_uart(example2 0)
pico_add_extra_outputs(example2)

add_executable(example3 example3.c)
target_link_libraries(example3 pico_stdlib hardware_pio dshot)
pico_enable_stdio_usb(example3 1)
pico_enable_stdio_uart(example3 0)
pico_add_extra_outputs(example3)

