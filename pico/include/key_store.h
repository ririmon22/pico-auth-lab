#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEY_STORE_MAX_KEY 4

typedef struct {
  bool used;
  uint8_t handle[8];
  uint8_t private_key[32];
  uint8_t public_key;
} KeySlot;
