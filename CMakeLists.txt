# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-fexceptions)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-fno-unwind-tables" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-fno-rtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-fno-use-cxa-atexit" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

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
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(Pico_AutoInput C CXX ASM)

pico_sdk_init()

add_executable(Pico_AutoInput
    Pico_AutoInput.cpp
    ScriptProcessor.cpp
    WS2812.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/PNGdec.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/adler32.c
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/crc32.c
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/infback.c
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/inffast.c
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/inflate.c
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/inftrees.c
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src/zutil.c

    # pico-littlefs-usb (littlefs-based USB MSC)
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/littlefs_driver.c
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/mimic_fat.c
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/usb_msc_driver.c
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/unicode.c

    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.cpp

    ${CMAKE_CURRENT_LIST_DIR}/TinyUSB_Mouse_and_Keyboard/TinyUSB_Mouse_and_Keyboard.cpp
    ${CMAKE_CURRENT_LIST_DIR}/SwitchControllerPico/src/SwitchControllerPico.cpp
    ${CMAKE_CURRENT_LIST_DIR}/SwitchControllerPico/src/NintendoSwitchControllPico.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tinyexpr-plusplus/tinyexpr.cpp
)

pico_set_program_name(Pico_AutoInput "Pico_AutoInput")
pico_set_program_version(Pico_AutoInput "0.1")
 
pico_generate_pio_header(Pico_AutoInput ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
pico_generate_pio_header(Pico_AutoInput ${CMAKE_CURRENT_LIST_DIR}/WS2812.pio)

pico_enable_stdio_uart(Pico_AutoInput 1)
pico_enable_stdio_usb(Pico_AutoInput 0)

target_compile_definitions(Pico_AutoInput PRIVATE
    __PICO__=1
    USE_TINYUSB
)

# Provide littlefs vendor sources as INTERFACE library
add_library(littlefs INTERFACE)
target_sources(littlefs INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/vendor/littlefs/lfs.c
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/vendor/littlefs/lfs_util.c
)
target_include_directories(littlefs INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/vendor/littlefs
)
target_compile_options(littlefs INTERFACE -Wno-unused-function -Wno-null-dereference)
target_link_libraries(littlefs INTERFACE hardware_flash)

target_include_directories(Pico_AutoInput PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/include
    ${CMAKE_CURRENT_LIST_DIR}/pico-littlefs-usb/vendor/littlefs
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/PNGdec/src
    ${CMAKE_CURRENT_LIST_DIR}/TinyUSB_Mouse_and_Keyboard
    ${CMAKE_CURRENT_LIST_DIR}/SwitchControllerPico/src
    ${CMAKE_CURRENT_LIST_DIR}/tinyexpr-plusplus
)

target_link_libraries(Pico_AutoInput
    hardware_flash
    hardware_sync
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_pio
    hardware_interp
    hardware_timer
    hardware_watchdog
    hardware_clocks
    tinyusb_additions
    tinyusb_board
    tinyusb_device
    pico_stdlib
    littlefs
)

target_compile_options(Pico_AutoInput PRIVATE -mthumb -mcpu=cortex-m0plus)
target_link_options(Pico_AutoInput PRIVATE -mthumb -mcpu=cortex-m0plus)

pico_add_extra_outputs(Pico_AutoInput)
