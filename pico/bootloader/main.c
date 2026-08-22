#include "pico/stdlib.h"

#include "hardware/structs/nvic.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "pico/stdio.h"

#include <stdint.h>
#include <stdio.h>

#define APP_FLASH_START  0x10010000u
#define APP_VECTOR_TABLE (APP_FLASH_START + 0x100u)
#define FLASH_END        0x10200000u
#define APP_SRAM_START   0x20000000u
#define APP_SRAM_END     0x20042000u

static bool app_vectors_are_valid(uint32_t app_msp, uint32_t app_reset);
static void jump_to_app(void) __attribute__((noreturn));

int
main(void) {
    const uint32_t *vectors = (const uint32_t *)APP_VECTOR_TABLE;

    stdio_init_all();

    if (!app_vectors_are_valid(vectors[0], vectors[1])) {
        for (;;) {
            printf("Bootloader: invalid application vectors\n");
            sleep_ms(1000u);
        }
    }

    stdio_deinit_all();
    jump_to_app();
}

static bool
app_vectors_are_valid(uint32_t app_msp, uint32_t app_reset) {
    const uint32_t reset_address = app_reset & ~1u;

    return (app_msp >= APP_SRAM_START) && (app_msp <= APP_SRAM_END)
           && ((app_msp & 0x7u) == 0u)
           && ((app_reset & 1u) != 0u)
           && (reset_address >= APP_VECTOR_TABLE)
           && (reset_address < FLASH_END);
}

static void
jump_to_app(void) {
    const uint32_t *vectors = (const uint32_t *)APP_VECTOR_TABLE;
    const uint32_t  app_msp = vectors[0];
    const uint32_t  app_reset = vectors[1];

    __asm volatile("cpsid i" ::: "memory");

    systick_hw->csr = 0u;
    systick_hw->cvr = 0u;
    nvic_hw->icer = UINT32_MAX;
    nvic_hw->icpr = UINT32_MAX;

    /* Clear pending PendSV and SysTick, then use the application's vectors. */
    scb_hw->icsr = (1u << 27u) | (1u << 25u);
    scb_hw->vtor = APP_VECTOR_TABLE;

    __asm volatile(
        "dsb\n"
        "isb\n"
        "msr msp, %0\n"
        "cpsie i\n"
        "bx %1\n"
        :
        : "r"(app_msp), "r"(app_reset)
        : "memory");

    __builtin_unreachable();
}
