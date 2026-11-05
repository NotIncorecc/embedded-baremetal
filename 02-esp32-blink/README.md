# 02 – ESP32 LED blink (bare-metal Xtensa)

Blinks an external LED on GPIO2 of an ESP32-WROOM-32 dev board with no ESP-IDF, no FreeRTOS and no libc. The program is flashed where the ROM looks for its second-stage bootloader, so the ROM jumps straight into it.

## Architecture

```
power-on → ROM bootloader → reads flash @ 0x1000 → jumps to call_start_cpu0()
                                                      │
        ┌─────────────────────────────────────────────┘
        ▼
  1. disable 3 watchdogs (RTC, TIMG0, TIMG1)   ROM leaves them running; they would reset the chip
  2. IO_MUX: pad GPIO2 → GPIO function
     GPIO matrix: output signal 256 (plain GPIO) → GPIO2, output enable on
  3. loop forever:
        W1TS → pin high,  send '1' on UART0,  wait 2 s
        W1TC → pin low,   send '0' on UART0,  wait 2 s
```

| Piece | What it does |
|---|---|
| `esp32.ld` | Linker script. Code goes to internal IRAM (0x40080000) and constants to DRAM (0x3FFB0000). `ENTRY(call_start_cpu0)` is the entry the ROM jumps to. |
| `build_esp32.sh` | Compiles freestanding (`-nostdlib -nostartfiles`), then `esptool elf2image` wraps the ELF in the image format the ROM expects. |
| Register access | `REG(addr)` is a `volatile uint32_t*` dereference. Addresses were checked against the ESP-IDF v5.4 headers. |
| Timing | Polled, not interrupt-driven. It reads the Xtensa `CCOUNT` cycle counter and waits for N cycles. The CPU is assumed to run at 40 MHz (the crystal) after ROM boot, and the 2 s blink period confirmed that. |
| UART0 heartbeat | Writes `'1'`/`'0'` to the UART0 FIFO at each toggle. The ROM already set 115200 baud for its boot banner, so a serial terminal shows the blink too. |

The dev board has no GPIO-controlled user LED (its LED shows serial activity), so the LED is external.

## Wiring

```
ESP32 GPIO2 ──[ resistor ]──►|── GND        (LED anode toward the resistor, cathode to GND)
```

Use a current-limiting resistor (a few hundred ohms is typical).

## Layout

```
02-esp32-blink/
├── src/blink.c                 the firmware
├── src/esp32.ld                linker script
├── build_esp32.sh              build (and optionally flash)
├── build/                      output (.elf, .bin, .map)
└── lessons/
    ├── esp-blink-baremetal.md        the chat this project came from
    └── esp-blink-line-by-line.md     line-by-line walkthrough of blink.c
```

## Requirements

- `esptool` (the scripts call `esptool`, v5.x syntax).
- The Xtensa cross-compiler, `xtensa-esp32-elf-gcc`. The script looks in `~/.local/opt/xtensa-esp-elf-16.1.0/bin`; override it with `TC=/path/to/bin`.
- Serial access to the board (`dialout` group). The ESP32 uses a CP2102 bridge; the Arduino clone uses a CH340. To tell them apart when both are plugged in: `ls -l /dev/serial/by-id/`.

## Commands (run from this folder)

```bash
./build_esp32.sh                       # build build/blink.bin
./build_esp32.sh flash                 # build, then flash to 0x1000
PORT=/dev/ttyUSB1 ./build_esp32.sh flash   # different port (default /dev/ttyUSB0)

# Optional: watch the '1'/'0' heartbeat
picocom -b 115200 /dev/ttyUSB0         # exit with Ctrl-A then Ctrl-X
```
