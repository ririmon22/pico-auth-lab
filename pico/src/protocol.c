#include "protocol.h"
#include "hex.h"

#include <pico/stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t key[CHALLENGE_BYTES] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB,
                                             0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76,
                                             0x98, 0xBA, 0xDC, 0xFE};

static Packet packet;

static void protocol_init(void) {
  packet.command[0] = '\0';
  packet.context[0] = '\0';
}

void protocol_handle_line(char *buf) {
  protocol_init();

  char *token = strtok(buf, " \r\n");
  if (token != NULL) {
    if (strlen(token) >= CMD_BUF_SIZE) {
      printf("ERR CMD_TOO_LONG\n");
      fflush(stdout);
      return;
    }
    snprintf(packet.command, CMD_BUF_SIZE, "%s", token);

    token = strtok(NULL, " \r\n");
    if (token != NULL) {
      if (strlen(token) >= RX_BUF_SIZE - CMD_BUF_SIZE) {
        printf("ERR CONTEXT_TOO_LONG\n");
        fflush(stdout);
        return;
      }
      snprintf(packet.context, RX_BUF_SIZE - CMD_BUF_SIZE, "%s", token);
    } else {
      packet.context[0] = '\0';
    }
  }

  if (strcmp(packet.command, "PING") == 0) {
    printf("PONG\n");
  } else if (strcmp(packet.command, "GET_ID") == 0) {
    printf("OK ID pico-auth-lab\n");
  } else if (strcmp(packet.command, "CHALLENGE") == 0) {
    if (packet.context[0] == '\0') {
      printf("ERR MISSING_CHALLENGE\n");
    } else {
      if (strlen(packet.context) != CHALLENGE_HEX_LEN) {
        printf("ERR CHALLENGE_LENGTH\n");
        fflush(stdout);
        return;
      }
      if (!is_hex_string(packet.context)) {
        printf("ERR CONTEXT_NOT_HEX\n");
        fflush(stdout);
        return;
      }
      uint8_t out[CHALLENGE_BYTES];

      (void)hex_to_bytes(packet.context, out, CHALLENGE_BYTES);

      for (int i = 0; i < CHALLENGE_BYTES; i++) {
        out[i] ^= key[i];
      }

      (void)bytes_to_hex(out, CHALLENGE_BYTES, packet.context,
                         RX_BUF_SIZE - CMD_BUF_SIZE);

      printf("OK RESPONSE %s\n", packet.context);
    }
  } else {
    printf("ERR UNKNOWN_CMD\n");
  }
  fflush(stdout);
}
