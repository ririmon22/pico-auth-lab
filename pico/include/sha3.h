#ifndef SHA3_H
#define SHA3_H

#include <stddef.h>
#include <stdint.h>

#define SHA3_256_DIGEST_SIZE 32U

void sha3_256(const uint8_t *input, size_t input_len, uint8_t out[SHA3_256_DIGEST_SIZE]);

#endif
