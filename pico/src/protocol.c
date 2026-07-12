#include "protocol.h"
#include "hex.h""
#include "device_id.h"
#include "random_command.h"

#include <stdio.h>
#include <string.h>

static void packet_clear(Packet *out) {t->command[0] = '\0'; t->context[0]
                                             = '\0';
                                             
}

static int packet_parse(char *buf, Packet *out) {
  char *token = strtok(buf, " \r\n");

  if (token != NULL) {
    if (strlen(token) >= CMD_BUF_SIZE) {
      printf("ERR CMD_TOO_LONG\n");
      return -1;
    }
    snprintf(out->command, CMD_BUF_SIZE, "%s", token);

    token = strtok(NULL, " \r\n");
    if (token != NULL) {
      if (strlen(token) >= RX_BUF_SIZE - CMD_BUF_SIZE) {
        printf("ERR CONTEXT_TOO_LONG\n");
        return -1;
      }
      snprintf(out->context, RX_BUF_SIZE - CMD_BUF_SIZE, "%s", token);
    }
  }

  return 0;
}

void protocol_handle_line(char *buf) {
  Packet packet;

  packet_clear(&packet);

  if (packet_parse(buf, &packet) != 0) {
    fflush(stdout);
    return;
  }

  if (strcmp(packet.command, "GET_ID") == 0) {
    device_id_handle();
  } else if (strcmp(packet.command, "CHALLENGE") == 0) {
    challenge_handle(packet.context);
  } else if (strcmp(packet.command, "RANDOM") == 0) {
    random_command_handle(packet.context);
  } else {
    printf("ERR UNKNOWN_CMD\n");
  }
  fflush(stdout);
}
