#ifndef CHALLENGE_H
#define CHALLENGE_H

#define CHALLENGE_BYTES 16U
#define CHALLENGE_HEX_LEN (CHALLENGE_BYTES * 2U)
#define CHALLENGE_HEX_BUF_SIZE (CHALLENGE_HEX_LEN + 1U)

void challenge_handle(const char *challenge_hex);

#endif
