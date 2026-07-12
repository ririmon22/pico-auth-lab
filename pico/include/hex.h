#ifndef HEX_H
#define HEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool is_hex_string(const char *str);
int bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len);
int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len);
int hex_char_to_value(char c);

#endif
