#!/usr/bin/env bash
# Build (and optionally flash) src/blink.c with plain commands, no build tool.
# Usage: scripts/build.sh          -> build only
#        scripts/build.sh flash    -> build, then flash
#        PORT=/dev/ttyUSB1 scripts/build.sh flash
set -euo pipefail

cd "$(dirname "$0")/.."

MCU=atmega328p
F_CPU=16000000UL
PORT="${PORT:-/dev/ttyUSB0}"
BAUD=115200

mkdir -p build

# 1. Compile + link C source into an ELF executable
avr-gcc -mmcu=$MCU -DF_CPU=$F_CPU -Os -std=gnu99 -Wall -Wextra \
    -o build/blink.elf src/blink.c

# 2. Extract the flash contents as Intel HEX
avr-objcopy -O ihex -j .text -j .data build/blink.elf build/blink.hex

# 3. Show how much flash/RAM is used
avr-size --mcu=$MCU -C build/blink.elf

# 4. Upload through the Optiboot bootloader (only when asked)
if [[ "${1:-}" == "flash" ]]; then
    avrdude -c arduino -p m328p -P "$PORT" -b $BAUD -U flash:w:build/blink.hex:i
fi
