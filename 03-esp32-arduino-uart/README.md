# 03 – UART link: ESP32 dashboard ↔ Arduino (AVR) peripherals

An ESP32 text dashboard controls peripherals wired to an Uno-style ATmega328P over UART. Both sides are bare-metal C: avr-libc on the AVR, direct registers on the ESP32 (no ESP-IDF, no libc). You type commands in a serial terminal on the ESP32, the ESP32 checks them and forwards them, and the AVR parses them and drives two SG90 servos and an LED.

## Architecture

```
 you (picocom, UART0 115200) ◄──► ESP32 dashboard ◄──UART2 9600 8N1──► AVR periph_uart
                                  (validates, forwards)                (parses, drives pins)
                                                                           │
                                                          D9 ─ servo (Timer1 OC1A)
                                                          D10 ─ servo (Timer1 OC1B)
                                                          D6 ─ LED   (Timer0 PWM)
```

**Who owns what**

- **The AVR owns the peripheral table.** The pin table is the designer's declaration, because the MCU can't detect what is wired to a pin. It is the single source of truth.
- **The ESP32 is the judge.** It asks the AVR for the table with `?`, checks every command against it, and only then sends. The AVR still parses and re-validates.
- **The ESP32 never waits for a reply.** It polls both UARTs in one loop and prints `AVR: ...` lines whenever they arrive. If nothing comes back within 200 ms it prints `AVR: no reply`, so a dead AVR is visible.

**Protocol (text lines ending in `\n`)**

| Direction | Message | Meaning |
|---|---|---|
| ESP32 → AVR | `S<pin><angle>` e.g. `S09090` | Servo on pin 09 to angle 000–180 |
| ESP32 → AVR | `L<pin><percent>` e.g. `L06050` | LED on pin 06 to 000–100 % brightness |
| ESP32 → AVR | `?` | Send me your table |
| AVR → ESP32 | `OK <cmd>` | Output updated (open loop: a servo can't report that it moved) |
| AVR → ESP32 | `ERR INVALID` | Bad format, out of range, or pin not in the table |
| AVR → ESP32 | `ERR` | UART framing/overrun error |
| AVR → ESP32 | `BOOT` | Sent once at reset, so a brownout is visible in the log |
| AVR → ESP32 | `T S09 180` … `T END` | Table rows, then an end marker |

To add a peripheral on the AVR, add a row to `cmd_types[]` (new type only) and one to `outputs[]` in `avr/periph_uart.c`.

**ESP32 side:** `esp32/dashboard.c` configures UART0 (115200, already set up by ROM) and UART2 (9600, via clock divider), routes UART2 to GPIO17/GPIO16 through the GPIO matrix, and disables the watchdogs. The menu is `1) View peripherals` and `2) Send command`. Input is collected until Enter, and backspace is handled. All state lives on the stack because the linker script has no `.data`/`.bss`.

**AVR side:** `avr/periph_uart.c` runs USART0 at 9600 8N1, polled. Servos use Timer1 mode 14 (Fast PWM, 20 ms period, 1–2 ms pulses). The LED uses Timer0 Fast PWM.

## Wiring

```
ESP32 GPIO17 (U2TXD) ───────────────────────────► AVR D0 (RX)
AVR D1 (TX) ──[10k]──┬──► ESP32 GPIO16 (U2RXD)     divider: 5 V → 3.33 V
                    [10k]
                      │
ESP32 GND ────────────┴────────────────────────► AVR GND
ESP32 VIN/5V ───────────────────────────────────► AVR 5V pin (powers the AVR, 2 servos, LED)
```

- **The AVR's TX line is 5 V and the ESP32's pins are 3.3 V.** The divider is required on D1 → GPIO16. The other direction (3.3 V into the AVR) is fine without one.
- **The AVR's USB must be unplugged while it is powered from the ESP32.**
- **D0 is shared with the CH340 USB bridge.** Unplug the GPIO17 → D0 wire before uploading to the AVR, then plug it back in. Known compromise: the CH340's TX pushes about 1.7 mA into GPIO17 when high. A planned fix is a diode plus an open-drain GPIO17.
- Peripherals: servos on D9 and D10, LED on D6.

## Layout

```
03-esp32-arduino-uart/
├── avr/
│   ├── periph_uart.c   AVR firmware: UART command parser + servo/LED drivers
│   └── power_test.c    one-shot supply self-test (servos + LED, prints each step)
├── esp32/
│   ├── dashboard.c     ESP32 firmware: text dashboard + relay
│   └── esp32.ld        linker script
├── Makefile            AVR build/flash (PROG selects avr/$(PROG).c)
├── build_esp32.sh      ESP32 build/flash (APP selects esp32/$APP.c)
├── build/              output (AVR .elf/.hex, ESP32 in build/esp32/)
└── lessons/esp-arduino-uart.md   the chat this project came from
```

## Requirements

Everything from projects 01 and 02 (`avr-gcc`, `avrdude`, `make`, `esptool`, the Xtensa toolchain), plus `picocom` as a serial terminal (`sudo apt install picocom`). With both boards plugged in, find which port is which: `ls -l /dev/serial/by-id/` (CP2102 = ESP32, CH340 = AVR). The commands below assume the AVR is `/dev/ttyUSB0`; pass `PORT=` when it isn't.

## Commands (run from this folder)

**1. Test the power supply first (AVR only; recommended after changing the wiring)**

```bash
make PROG=power_test                  # build
make PROG=power_test PORT=/dev/ttyUSB0 flash
picocom -b 9600 --omap crlf --echo /dev/ttyUSB0
# Expect: BOOT, then TEST <cmd> lines, ending TEST DONE.
# A second BOOT mid-run means a reset (brownout); the last TEST line names the step.
# Exit picocom with Ctrl-A then Ctrl-X.
```

**2. Flash the AVR firmware** (with the GPIO17 → D0 wire unplugged)

```bash
make                                  # build build/periph_uart.hex
make flash                            # PORT=/dev/ttyUSB0 by default
make clean
```

You can also test the AVR alone from a terminal on its USB port, before connecting the ESP32. Type commands like `S09090` or `L06050`, and try bad ones such as `abc` or `999`:

```bash
picocom -b 9600 --omap crlf --echo /dev/ttyUSB0
```

**3. Flash the ESP32 dashboard**

```bash
./build_esp32.sh                      # build build/esp32/dashboard.bin
PORT=/dev/ttyUSB1 ./build_esp32.sh flash
```

**4. Run it**

Reconnect the GPIO17 → D0 wire, power the AVR from the ESP32's 5 V (AVR USB unplugged), then open the ESP32's port. The dashboard does its own echo, so don't pass `--echo`:

```bash
picocom -b 115200 /dev/ttyUSB1
# Menu: 1) View peripherals  2) Send command  — e.g. S09090 then Enter
```

## Status

Verified on hardware: power under full servo load and the AVR → ESP32 direction (`AVR: ...` lines). Not yet verified: the ESP32 → AVR direction (commands and the table query through the dashboard). The SG90 angle accuracy is parked: the commanded angle doesn't always match the rotation (suspects: pulse range of 1–2 ms narrower than the SG90 travel, USB 5 V sag).
