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
	set(DEFAULT_SYSCLOCK 200)
elseif(PICO_TARGET STREQUAL "PICO2")
	set(PICO_BOARD pico2 CACHE STRING "Board type" FORCE)
	add_compile_definitions(TARGET_PICO2=1)
	set(DEFAULT_SYSCLOCK "")	# SDK default (150 MHz)
else()
	message(FATAL_ERROR "Invalid PICO_TARGET='${PICO_TARGET}'. Use PICO or PICO2.")
endif()

# System clock in MHz (-DSYSCLOCK=nnn) for both boards. The Pico defaults to 200 MHz; the Pico 2 keeps the
# SDK default of 150 MHz unless SYSCLOCK is given. The SDK applies the clock before main() (SDK 2.1.1 or later).
if("${SYSCLOCK}" STREQUAL "")
	set(SYSCLOCK_MHZ "${DEFAULT_SYSCLOCK}")
else()
	set(SYSCLOCK_MHZ "${SYSCLOCK}")
endif()

if(NOT "${SYSCLOCK_MHZ}" STREQUAL "")
	add_compile_definitions(SYS_CLK_MHZ=${SYSCLOCK_MHZ})
endif()

message(STATUS "Building for ${PICO_BOARD} (${PICO_TARGET})")

# ---- Performance / behaviour options -------------------------------------------
# Use e.g.  cmake -B build -S . -DSD_FLUSH_DELAY_MS=0   (the SD clock is raised with SDKHZ in system.cfg)
set(SD_SPI_KHZ 5000 CACHE STRING "SD card SPI boot clock in kHz, used to mount the card and read system.cfg. SDKHZ in system.cfg can raise it afterwards (5000 = original firmware)")
set(SD_FLUSH_DELAY_MS 200 CACHE STRING "Idle time (ms) before buffered SD writes are synced; 0 = sync after every write")
set(SD_FLUSH_MAX_MS 2000 CACHE STRING "Longest time (ms) unsynced writes may stay buffered")
option(SD_CRC_ENABLED "Check CRC on SD card data blocks (slightly slower, more robust)" ON)
option(FDC_LOGGING "Log Z80 register accesses for the CLI log command (slows the bus path)" OFF)

math(EXPR SD_SPI_BAUD_HZ_VALUE "${SD_SPI_KHZ} * 1000")
add_compile_definitions(SD_SPI_BAUD_HZ=${SD_SPI_BAUD_HZ_VALUE}
                        SD_FLUSH_DELAY_MS=${SD_FLUSH_DELAY_MS}
                        SD_FLUSH_MAX_MS=${SD_FLUSH_MAX_MS})
if(SD_CRC_ENABLED)
	add_compile_definitions(SD_CRC_ENABLED=1)
else()
	add_compile_definitions(SD_CRC_ENABLED=0)
endif()
if(FDC_LOGGING)
	add_compile_definitions(ENABLE_LOGGING=1)
endif()


# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if(DEFINED PICO_SDK_VERSION_STRING)
	message(STATUS "Pico SDK version ${PICO_SDK_VERSION_STRING}")

	if((NOT "${SYSCLOCK_MHZ}" STREQUAL "") AND (PICO_SDK_VERSION_STRING VERSION_LESS "2.1.1"))
		message(FATAL_ERROR "Pico SDK ${PICO_SDK_VERSION_STRING} is too old: the system clock (${SYSCLOCK_MHZ} MHz) is set through SYS_CLK_MHZ, which needs SDK 2.1.1 or later")
	endif()
endif()

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
