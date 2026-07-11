#include <pico/stdio.h>
#include <pico/time.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  stdio_init_all();
  sleep_ms(2000);

  printf("Pico USB ready\n");

  char buf[128];

  while (true) {
    printf("> ");
    fflush(stdout);

    if (fgets(buf, sizeof(buf), stdin) != NULL) {
      size_t len = strlen(buf);

      if ((len > 0U) && (buf[len - 1U] == '\n')) {
        buf[len - 1U] = '\0';
      }

      printf("受信: %s\n", buf);
      fflush(stdout);
    }
  }
}
