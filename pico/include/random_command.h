#ifndef RANDOM_COMMAND_H
#define RANDOM_COMMAND_H

#define RANDOM_MAX_BYTES 32U
#define RANDOM_HEX_BUF_SIZE ((RANDOM_MAX_BYTES * 2U) + 1U)

void random_command_handle(const char *len_text);

#endif
