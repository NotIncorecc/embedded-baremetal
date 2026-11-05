#!/usr/bin/env bash
# Build (and optionally flash) a bare-metal ESP32 program: src/$APP.c
# Usage: [APP=dashboard] scripts/build_esp32.sh [flash]
#        APP=blink (default)  PORT=/dev/ttyUSB0  BAUD=115200 (all overridable)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TC="${TC:-$HOME/.local/opt/xtensa-esp-elf-16.1.0/bin}"
APP="${APP:-blink}"
PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-115200}"
OUT="$ROOT/build"
mkdir -p "$OUT"

if [[ ! -f "$ROOT/src/$APP.c" ]]; then
    echo "no such program: src/$APP.c" >&2
    exit 1
fi

# Freestanding: no libc, no startup files; ROM jumps straight to call_start_cpu0.
"$TC/xtensa-esp32-elf-gcc" -Os -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
    -mlongcalls -mtext-section-literals \
    -T "$ROOT/src/esp32.ld" -Wl,-Map,"$OUT/$APP.map" \
    "$ROOT/src/$APP.c" -o "$OUT/$APP.elf"

# Wrap the ELF in the image format the ROM bootloader understands.
esptool --chip esp32 elf2image --flash-mode dio --flash-freq 40m --flash-size 4MB \
    -o "$OUT/$APP.bin" "$OUT/$APP.elf"

"$TC/xtensa-esp32-elf-size" "$OUT/$APP.elf"

if [[ "${1:-}" == "flash" ]]; then
    # 0x1000 is where the ROM looks for its 2nd-stage bootloader; we put our program there.
    esptool --chip esp32 --port "$PORT" --baud "$BAUD" write-flash 0x1000 "$OUT/$APP.bin"
fi
