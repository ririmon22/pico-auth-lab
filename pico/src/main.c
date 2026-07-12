#include "protocol.h"

#include <pico/stdio.h>
#include <stdbool.h>
#include <stdio.h>

int main(void) {
  stdio_init_all();

  printf("Pico USB ready\n");

  char buf[RX_BUF_SIZE];

  while (true) {
    fflush(stdout);

    if (fgets(buf, sizeof(buf), stdin) != NULL) {
      protocol_handle_line(buf);
    }
  }
}
