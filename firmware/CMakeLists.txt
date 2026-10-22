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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
#set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_TARGET "PICO2" CACHE STRING "Target board: PICO or PICO2")
set_property(CACHE PICO_TARGET PROPERTY STRINGS PICO PICO2)

if(PICO_TARGET STREQUAL "PICO")
	set(PICO_BOARD pico CACHE STRING "Board type" FORCE)
	add_compile_definitions(TARGET_PICO=1)
	if("${SYSCLOCK}" STREQUAL "")
	  add_compile_definitions(SYS_CLOCK=200000)
	else()
        	math(EXPR SYS_CLOCK_KHZ "${SYSCLOCK} * 1000")
		add_compile_definitions(SYS_CLOCK=${SYS_CLOCK_KHZ})
	endif()
elseif(PICO_TARGET STREQUAL "PICO2")
	set(PICO_BOARD pico2 CACHE STRING "Board type" FORCE)
	add_compile_definitions(TARGET_PICO2=1)
else()
	message(FATAL_ERROR "Invalid PICO_TARGET='${PICO_TARGET}'. Use PICO or PICO2.")
endif()

message(STATUS "Building for ${PICO_BOARD} (${PICO_TARGET})")


# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Floppy80 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()
add_subdirectory(lib/no-OS-FatFS-SD-SPI-RPi-Pico-master/FatFs_SPI build)

# Add executable. Default name is the project name, version 0.1

add_executable(Floppy80
    main.c
    fdc.c
    hw_config.c
    sd_core.c
    system.c
    crc.c
    file.c
    cli.c
    memory.c
    logging.c
    hdc.c
)

# pull in common dependencies
target_link_libraries(Floppy80
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_irq
    FatFs_SPI
)

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(Floppy80 pico_cyw43_arch_none)
endif()

# create map/bin/hex file etc.
pico_add_extra_outputs(Floppy80)

# add url via pico_set_program_url
