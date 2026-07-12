#include "hex.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool is_hex_string(const char *str) {
  size_t len = strlen(str);

  if (len == 0 || (len % 2) != 0) {
    return false;
  }

  while (*str != '\0') {
    if (!isxdigit((unsigned char)*str)) {
      return false;
    }
    str++;
  }
  return true;
}

int hex_char_to_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  return -1;
}

int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
  for (size_t i = 0; i < out_len; i++) {
    int hi = hex_char_to_value(hex[i * 2]);
    int lo = hex_char_to_value(hex[i * 2 + 1]);

    if (hi < 0 || lo < 0)
      return -1;

    out[i] = (hi << 4) | lo;
  }

  return 0;
}

int bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len) {
  if (out_len < len * 2 + 1) {
    return -1;
  }

  for (size_t i = 0; i < len; i++) {
    snprintf(&out[i * 2], 3, "%02x", bytes[i]);
  }

  out[len * 2] = '\0';

  return 0;
}
