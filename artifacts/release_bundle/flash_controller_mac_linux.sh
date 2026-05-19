#!/usr/bin/env bash
set -euo pipefail
PORT="${1:-/dev/ttyUSB0}"
python3 -m esptool --chip esp32 -p "$PORT" -b 115200 --before default_reset --after hard_reset write_flash 0x0 controller/curie_controller_full_4mb.bin
