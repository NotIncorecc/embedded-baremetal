#!/usr/bin/env bash
# Build (and optionally flash) the bare-metal ESP32 blink.
# Usage: scripts/build_esp32.sh [flash]      PORT=/dev/ttyUSB0 BAUD=115200 (overridable)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${TC:-$HOME/.local/opt/xtensa-esp-elf-16.1.0/bin}"
PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-115200}"
OUT="$ROOT/build/esp32"
mkdir -p "$OUT"

# Freestanding: no libc, no startup files; ROM jumps straight to call_start_cpu0.
"$TC/xtensa-esp32-elf-gcc" -Os -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
    -mlongcalls -mtext-section-literals \
    -T "$ROOT/src/esp32/esp32.ld" -Wl,-Map,"$OUT/blink.map" \
    "$ROOT/src/esp32/blink.c" -o "$OUT/blink.elf"

# Wrap the ELF in the image format the ROM bootloader understands.
esptool --chip esp32 elf2image --flash-mode dio --flash-freq 40m --flash-size 4MB \
    -o "$OUT/blink.bin" "$OUT/blink.elf"

"$TC/xtensa-esp32-elf-size" "$OUT/blink.elf"

if [[ "${1:-}" == "flash" ]]; then
    # 0x1000 is where the ROM looks for its 2nd-stage bootloader; we put our program there.
    esptool --chip esp32 --port "$PORT" --baud "$BAUD" write-flash 0x1000 "$OUT/blink.bin"
fi
