#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT="${BUILD_DIR}/pico_auth_pc"
DEVICE_PATH="${1:-/dev/ttyACM0}"
MESSAGE="${2:-Hello}"

mkdir -p "$BUILD_DIR"

cc -std=c99 -Wall -Wextra -pedantic \
  "${SCRIPT_DIR}/src/main.c" \
  -o "$OUTPUT"

if [ "${1:-}" = "build" ]; then
  printf 'Built %s\n' "$OUTPUT"
  exit 0
fi

exec "$OUTPUT" "$DEVICE_PATH" "$MESSAGE"
