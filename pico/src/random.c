#include "random.h"

#include <pico/rand.h>

void random_bytes(uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i++) {
    out[i] = get_rand_32() & 0xFF;
  }
}
