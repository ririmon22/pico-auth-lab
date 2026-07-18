#include "sha3.h"

#include <stdint.h>
#include <string.h>

#define SHA3_256_RATE      136U
#define KECCAK_STATE_WORDS 25U
#define KECCAK_ROUNDS      24U

static uint64_t load64_le(const uint8_t *src);
static void     store64_le(uint8_t *dst, uint64_t value);
static void     keccak_f1600(uint64_t state[KECCAK_STATE_WORDS]);
static uint64_t rotl64(uint64_t value, unsigned int shift);

void
sha3_256(const uint8_t *input, size_t input_len, uint8_t out[SHA3_256_DIGEST_SIZE]) {
    uint64_t state[KECCAK_STATE_WORDS];
    uint8_t  block[SHA3_256_RATE];

    memset(state, 0, sizeof(state));

    while (input_len >= SHA3_256_RATE) {
        for (size_t i = 0; i < SHA3_256_RATE / 8U; i++) {
            state[i] ^= load64_le(&input[i * 8U]);
        }

        keccak_f1600(state);

        input += SHA3_256_RATE;
        input_len -= SHA3_256_RATE;
    }

    memset(block, 0, sizeof(block));
    memcpy(block, input, input_len);

    block[input_len] = 0x06U;
    block[SHA3_256_RATE - 1U] |= 0x80U;

    for (size_t i = 0; i < SHA3_256_RATE / 8U; i++) {
        state[i] ^= load64_le(&block[i * 8U]);
    }

    keccak_f1600(state);

    for (size_t i = 0; i < SHA3_256_DIGEST_SIZE / 8U; i++) {
        store64_le(&out[i * 8U], state[i]);
    }
}

static uint64_t
load64_le(const uint8_t *src) {
    uint64_t value = 0U;

    for (size_t i = 0; i < 8U; i++) {
        value |= ((uint64_t)src[i]) << (8U * i);
    }

    return value;
}

static void
store64_le(uint8_t *dst, uint64_t value) {
    for (size_t i = 0; i < 8U; i++) {
        dst[i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
    }
}

static void
keccak_f1600(uint64_t state[KECCAK_STATE_WORDS]) {
    static const uint64_t round_constants[KECCAK_ROUNDS] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL, 0x8000000080008000ULL,
        0x000000000000808BULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008AULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL,
        0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800AULL, 0x800000008000000AULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
    };

    static const unsigned rho_offsets[KECCAK_STATE_WORDS] = {
        0U,  1U,  62U, 28U, 27U, 36U, 44U, 6U,  55U, 20U, 3U,  10U, 43U,
        25U, 39U, 41U, 45U, 15U, 21U, 8U,  18U, 2U,  61U, 56U, 14U,
    };
    for (size_t round = 0; round < KECCAK_ROUNDS; round++) {
        uint64_t c[5];
        uint64_t d[5];
        uint64_t b[KECCAK_STATE_WORDS];

        /* Theta */
        for (size_t x = 0U; x < 5U; x++) {
            c[x] = state[x] ^ state[x + 5U] ^ state[x + 10U] ^ state[x + 15U] ^ state[x + 20];
        }

        for (size_t x = 0U; x < 5U; x++) {
            d[x] = c[(x + 4U) % 5U] ^ rotl64(c[(x + 1U) % 5U], 1U);
        }

        for (size_t y = 0U; y < 5U; y++) {
            for (size_t x = 0U; x < 5U; x++) {
                state[x + (5U * y)] ^= d[x];
            }
        }

        /* Rho + Pi */
        for (size_t y = 0U; y < 5U; y++) {
            for (size_t x = 0U; x < 5U; x++) {
                size_t src = x + (5U * y);
                size_t dst = y + (5U * ((2U * x + 3U * y) % 5U));

                b[dst] = rotl64(state[src], rho_offsets[src]);
            }
        }

        /* Chi */
        for (size_t y = 0U; y < 5U; y++) {
            for (size_t x = 0U; x < 5U; x++) {
                state[x + (5U * y)]
                    = b[x + (5U * y)]
                      ^ ((~b[((x + 1) % 5U) + (5U * y)]) & b[((x + 2U) % 5U) + (5U * y)]);
            }
        }

        /* Iota */
        state[0] ^= round_constants[round];
    }
}

static uint64_t
rotl64(uint64_t value, unsigned int shift) {
    if (shift == 0U) {
        return value;
    }

    return (value << shift) | (value >> (64U - shift));
}
