#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
TARGET_UF2="${BUILD_DIR}/pico_auth_lab.uf2"

if [ -z "${PICO_SDK_PATH:-}" ]; then
  export PICO_SDK_PATH="/home/ririmon/pico/pico-sdk"
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR"
printf 'Built UF2: %s\n' "$TARGET_UF2"

if [ "${1:-}" = "build" ]; then
  exit 0
fi

if ! command -v picotool >/dev/null 2>&1; then
  printf 'picotool not found.\n' >&2
else
  printf 'Loading UF2 with picotool: %s\n' "$TARGET_UF2"

  if picotool load -x -f "$TARGET_UF2"; then
    printf 'Loaded and started: %s\n' "$TARGET_UF2"
    exit 0
  fi

  printf 'picotool forced load failed. Trying direct load.\n' >&2

  if picotool load -x "$TARGET_UF2"; then
    printf 'Loaded and started: %s\n' "$TARGET_UF2"
    exit 0
  fi

  if command -v sudo >/dev/null 2>&1; then
    printf 'picotool direct load failed. Trying sudo picotool load.\n' >&2

    if sudo picotool load -x -f "$TARGET_UF2"; then
      printf 'Loaded and started with sudo: %s\n' "$TARGET_UF2"
      exit 0
    fi
  fi

  printf 'picotool load failed. Trying BOOTSEL UF2 copy fallback.\n' >&2
fi

UF2_MOUNT=$(
  find /media /run/media -maxdepth 3 -type d -name RPI-RP2 2>/dev/null | head -n 1 || true
)

if [ -n "$UF2_MOUNT" ]; then
  cp "$TARGET_UF2" "$UF2_MOUNT/"
  printf 'Copied %s to %s\n' "$TARGET_UF2" "$UF2_MOUNT"
  exit 0
fi

printf 'Built UF2: %s\n' "$TARGET_UF2" >&2
printf 'Could not access Pico through picotool and no RPI-RP2 BOOTSEL drive was found.\n' >&2
printf 'Hold BOOTSEL while plugging in the Pico, then run this script again.\n' >&2
printf 'If picotool should work, check USB permissions or try: sudo picotool load -x -f %s\n' "$TARGET_UF2" >&2
printf 'For permanent picotool access, install the udev rule:\n' >&2
printf '  sudo cp /home/ririmon/pico/picotool/udev/60-picotool.rules /etc/udev/rules.d/60-picotool.rules\n' >&2
printf '  sudo udevadm control --reload-rules\n' >&2
printf '  sudo udevadm trigger\n' >&2
printf 'Then unplug/replug the Pico and run this script again.\n' >&2
exit 1
