#include "sha3_command.h"
#include "hex.h"
#include "sha3.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SHA3_256_HEX_BUF_SIZE ((SHA3_256_DIGEST_SIZE * 2U) + 1U)

void
sha3_command_handle(const char *input_text) {
    uint8_t digest[SHA3_256_DIGEST_SIZE];
    char    digest_hex[SHA3_256_HEX_BUF_SIZE];

    if ((input_text == NULL) || (input_text[0] == '\0')) {
        printf("ERR MISSING_SHA3_INPUT\n");
        return;
    }

    sha3_256((const uint8_t *)input_text, strlen(input_text), digest);

    if (bytes_to_hex(digest, SHA3_256_DIGEST_SIZE, digest_hex, SHA3_256_HEX_BUF_SIZE) != 0) {
        printf("ERR INTERNAL\n");
        return;
    }

    printf("OK SHA3 %s\n", digest_hex);
}
