#include "random_command.h"
#include "hex.h"
#include "random.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int parse_random_len(const char *text, size_t *out_len);

void random_command_handle(const char *len_text) {
  size_t len = 0U;
  uint8_t out[RANDOM_MAX_BYTES];
  char out_hex[RANDOM_HEX_BUF_SIZE];

  if ((len_text == NULL) || (len_text[0] == '\0')) {
    printf("ERR MISSING_RANDOM_LEN\n");
    return;
  }

  if (parse_random_len(len_text, &len) != 0) {
    printf("ERR RANDOM_LEN\n");
    return;
  }

  random_bytes(out, len);

  if (bytes_to_hex(out, len, out_hex, RANDOM_HEX_BUF_SIZE) != 0) {
    printf("ERR INTERNAL\n");
    return;
  }

  printf("OK RANDOM %s\n", out_hex);
}

static int parse_random_len(const char *text, size_t *out_len) {
  size_t value = 0U;

  if ((text == NULL) || (out_len == NULL) || (text[0] == '\0')) {
    return -1;
  }

  for (size_t i = 0U; text[i] != '\0'; i++) {
    if ((text[i] < '0') || (text[i] > '9')) {
      return -1;
    }

    value = (value * 10U) + (size_t)(text[i] - '0');
    if (value > RANDOM_MAX_BYTES) {
      return -1;
    }
  }

  if (value == 0U) {
    return -1;
  }

  *out_len = value;
  return 0;
}
