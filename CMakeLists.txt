cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(dshot C ASM)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_library(dshot INTERFACE)
pico_generate_pio_header(dshot ${CMAKE_CURRENT_LIST_DIR}/src/dshot.pio)

target_sources(dshot INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/dshot.c)
target_include_directories(dshot INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(${PROJECT_NAME} INTERFACE
	pico_stdlib
	hardware_pio
)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

add_subdirectory(examples EXCLUDE_FROM_ALL)
