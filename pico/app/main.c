#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdio.h>

int
main(void) {
    stdio_init_all();

    while (true) {
        printf("Application runnning\n");
        sleep_ms(1000);
    }
}
