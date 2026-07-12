#include "challenge.h"
#include "hex.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t key[CHALLENGE_BYTES] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB,
                                             0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76,
                                             0x98, 0xBA, 0xDC, 0xFE};

void challenge_handle(const char *challenge_hex) {
  uint8_t response[CHALLENGE_BYTES];
  char response_hex[CHALLENGE_HEX_BUF_SIZE];

  if ((challenge_hex == NULL) || (challenge_hex[0] == '\0')) {
    printf("ERR MISSING_CHALLENGE\n");
    return;
  }

  if (strlen(challenge_hex) != CHALLENGE_HEX_LEN) {
    printf("ERR CHALLENGE_LENGTH\n");
    return;
  }

  if (!is_hex_string(challenge_hex)) {
    printf("ERR CONTEXT_NOT_HEX\n");
    return;
  }

  if (hex_to_bytes(challenge_hex, response, CHALLENGE_BYTES) != 0) {
    printf("ERR CONTEXT_NOT_HEX\n");
    return;
  }

  for (size_t i = 0U; i < CHALLENGE_BYTES; i++) {
    response[i] ^= key[i];
  }

  if (bytes_to_hex(response, CHALLENGE_BYTES, response_hex,
                   CHALLENGE_HEX_BUF_SIZE) != 0) {
    printf("ERR INTERNAL\n");
    return;
  }

  printf("OK RESPONSE %s\n", response_hex);
}
