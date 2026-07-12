cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_auth_lab C CXX ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_auth_lab
    src/main.c
    src/protocol.c
    src/hex.c
)

target_link_libraries(pico_auth_lab
    pico_stdlib
)

target_include_directories(pico_auth_lab
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_definitions(pico_auth_lab
    PRIVATE
        static_assert=_Static_assert
)

pico_enable_stdio_usb(pico_auth_lab 1)
pico_enable_stdio_uart(pico_auth_lab 0)

pico_add_extra_outputs(pico_auth_lab)
