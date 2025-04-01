# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(sd_card_v1 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(C:/Users/Lenovo/Desktop/Pmik/sd_library/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src build)

# Add executable. Default name is the project name, version 0.1

add_executable(sd_card_v1 sd_card_v1.c 
hw_config.c)

pico_set_program_name(sd_card_v1 "sd_card_v1")
pico_set_program_version(sd_card_v1 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(sd_card_v1 0)
pico_enable_stdio_usb(sd_card_v1 1)

# Add the standard library to the build
target_link_libraries(sd_card_v1
        pico_stdlib
        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
        

# Add the standard include files to the build
target_include_directories(sd_card_v1 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(sd_card_v1 


        )

pico_add_extra_outputs(sd_card_v1)

