#include <ctype.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdio.h>
#include <string.h>

#define RX_BUF_SIZE 128
#define CMD_BUF_SIZE 16
#define CHALLENGE_SIZE 33

typedef struct {
  char command[CMD_BUF_SIZE];
  char context[RX_BUF_SIZE - CMD_BUF_SIZE];
} Packet;

bool is_hex_string(const char *str);

int main(void) {
  stdio_init_all();
  sleep_ms(2000);

  printf("Pico USB ready\n");

  Packet packet;
  char buf[RX_BUF_SIZE];

  while (true) {
    packet.command[0] = '\0';
    packet.context[0] = '\0';
    fflush(stdout);

    if (fgets(buf, sizeof(buf), stdin) != NULL) {

      char *token = strtok(buf, " \r\n");
      if (token != NULL) {
        if (strlen(token) >= CMD_BUF_SIZE) {
          printf("ERR CMD_TOO_LONG\n");
          fflush(stdout);
          continue;
        }
        snprintf(packet.command, CMD_BUF_SIZE, "%s", token);

        token = strtok(NULL, " \r\n");
        if (token != NULL) {
          if (strlen(token) >= RX_BUF_SIZE - CMD_BUF_SIZE) {
            printf("ERR CONTEXT_TOO_LONG\n");
            fflush(stdout);
            continue;
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
          if (strlen(packet.context) >= CHALLENGE_SIZE) {
            printf("ERR CHALLENGE_TOO_LONG\n");
            fflush(stdout);
            continue;
          }
          if (!is_hex_string(packet.context)) {
            printf("ERR CONTEXT_NOT_HEX\n");
            fflush(stdout);
            continue;
          }
          printf("OK RESPONSE %s\n", packet.context);
        }
      } else {
        printf("ERR UNKNOWN_CMD\n");
      }
      fflush(stdout);
    }
  }
}

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
