#ifndef PROTOCOL_H
#define PROTOCOL_H

#define RX_BUF_SIZE 128
#define CMD_BUF_SIZE 16

typedef struct {
  char command[CMD_BUF_SIZE];
  char context[RX_BUF_SIZE - CMD_BUF_SIZE];
} Packet;

void protocol_handle_line(char *buf);

#endif
