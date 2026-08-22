cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_auth_lab C CXX ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(bootloader
  bootloader/main.c
)

target_link_libraries(bootloader
  pico_stdlib
)

pico_enable_stdio_usb(bootloader 1)
pico_enable_stdio_uart(bootloader 0)

target_compile_definitions(bootloader
  PRIVATE
    static_assert=_Static_assert
)

pico_add_extra_outputs(bootloader)

add_executable(app
  app/main.c
)

target_link_libraries(app
  pico_stdlib
)

pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 0)

target_compile_definitions(app
  PRIVATE
    static_assert=_Static_assert
)

pico_add_linker_script_override_path(
  app
  ${CMAKE_CURRENT_LIST_DIR}/linker
)

pico_add_extra_outputs(app)
