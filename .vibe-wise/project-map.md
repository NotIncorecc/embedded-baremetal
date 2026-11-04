# Project Map

## Purpose
Bare-metal C firmware (avr-libc, no Arduino core) for an Uno-style clone board. First milestone: blink the on-board LED.

## Requirements
- Language: C with avr-libc headers.
- Target: on-board LED of the connected board.
- No Arduino core library (implied by "bare-metal"; confirm if unclear).

## Components
- src/blink.c (implemented): PB5 output; Timer1 CTC, prescaler 1024, OCR1A = 31249 → 2 s; ISR(TIMER1_COMPA_vect) toggles PB5 (XOR, AI addition); empty main loop. Design by learner except XOR toggle, empty loop, and N-1 correction.
- scripts/build.sh (implemented): manual avr-gcc → avr-objcopy → avr-size → optional avrdude.
- Makefile (implemented): same steps as make targets all/flash/clean; incremental rebuild.

## Main Flow
  src/blink.c --avr-gcc--> build/blink.elf --avr-objcopy--> build/blink.hex --avrdude/Optiboot--> flash
  reset → main(): setup → sei() → idle loop
  Timer1 hits OCR1A every 2 s → ISR toggles PB5 → LED on/off

## Data and Trust Boundaries
No storage or external services. Hardware: board on /dev/ttyUSB0 via CH340.

## Build and Deployment
Verified on this machine: avrdude 7.1 at /usr/bin/avrdude; user in dialout group.
Verified probe: `avrdude -c arduino -p m328p -P /dev/ttyUSB0 -b 115200` reaches Optiboot 4.4, signature 0x1e950f.
Verified toolchain: avr-gcc 7.3.0, avr-objcopy, avr-size, make (all in /usr/bin); avr-libc headers at /usr/lib/avr/include.

Build: `scripts/build.sh [flash]` or `make` / `make flash` / `make clean`; PORT overridable.

## Unknowns
- Clock frequency assumed 16 MHz; not verified (fuses unreadable via Optiboot).
- Physical chip marking not yet confirmed (ATmega328P vs look-alike).

## ESP32 target (added 2026-11-04)
- Board: ESP32-D0WD rev v1.0, 4 MB flash, CP2102 USB bridge at /dev/ttyUSB0 (the earlier CH340 Arduino clone is not connected).
- Tools: esptool 5.6.0 via pipx; xtensa-esp32-elf-gcc 16.1.0 in ~/.local/opt/xtensa-esp-elf-16.1.0.
- src/esp32/blink.c: polled blink on GPIO2 using the Xtensa CCOUNT cycle counter, 2 s on/off; disables RTC and TIMG0/1 watchdogs; sends '1'/'0' on UART0 each toggle.
- Build/flash: scripts/build_esp32.sh [flash] (image flashed at 0x1000).
- Verified on hardware: external LED on GPIO2 blinks 2 s on/off. Board has no GPIO-controlled user LED (its LED is serial activity).
