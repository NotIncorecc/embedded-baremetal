# Project Map

## Purpose
Bare-metal C firmware for an Uno-style clone (avr-libc, no Arduino core) and an ESP32 dev board (no ESP-IDF). Blink milestones done on both. Current goal (2026-11-05): learn communication protocols by linking the two boards (UART first, then SPI, I2C), showing as many peripherals as possible from an ESP32 text dashboard.

## Requirements
- Language: C with avr-libc headers.
- Target: on-board LED of the connected board.
- No Arduino core library (implied by "bare-metal"; confirm if unclear).

## Components
- 01-arduino-blink/src/blink.c (implemented): PB5 output; Timer1 CTC, prescaler 1024, OCR1A = 31249 → 2 s; ISR(TIMER1_COMPA_vect) toggles PB5 (XOR, AI addition); empty main loop. Design by learner except XOR toggle, empty loop, and N-1 correction.
- 01-arduino-blink/scripts/build.sh (implemented): manual avr-gcc → avr-objcopy → avr-size → optional avrdude.
- Makefile (implemented; copies in 01-arduino-blink/ and 03-esp32-arduino-uart/): same steps as make targets all/flash/clean; incremental rebuild. PROG variable selects the source (01: src/$(PROG).c, default blink; 03: avr/$(PROG).c, default periph_uart).
- 03-esp32-arduino-uart/avr/periph_uart.c (implemented and hardware verified 2026-11-05; SG90 angle accuracy parked): USART0 9600 8N1 polled; commands S<pin><angle 000-180> and L<pin><pct 000-100> + '\n'; table-driven (cmd_types[], outputs[]); D9/D10 servos on Timer1 mode 14 (1-2 ms / 20 ms), D6 LED on Timer0 fast PWM; replies OK <cmd> / ERR INVALID / ERR; BOOT at reset; '?' -> table rows "T S09 180" ... "T END" (v3, not yet hardware tested).
- 03-esp32-arduino-uart/avr/power_test.c (implemented and hardware verified 2026-11-05): one-shot supply test; BOOT + 3 LED flashes, then 25 servo/LED steps (single, alternating, both at once, LED ramp), printing "TEST <cmd>" before each; ends "TEST DONE" with servo pulses off.

## Two-board link (hardware verified 2026-11-05)
  ESP32 GPIO17 (U2TXD) ─────────────────────► AVR D0 (shares the CH340 TX via ~1k; unplug before AVR uploads = option A)
  AVR D1 ──(10k||10k)──┬──► ESP32 GPIO16 (U2RXD)   divider 5 V → 3.33 V
                      10k
                       │
  ESP32 GND ───────────┴────────────────────► AVR GND
  ESP32 VIN/5V ─────────────────────────────► AVR 5V pin (powers the AVR, 2x SG90, LED; AVR USB must be unplugged)
- UART 9600 8N1. Verified: power under full servo load; AVR → ESP32 direction. Not yet verified: ESP32 → AVR direction via the dashboard.
- Known compromise (option A): CH340 TX pushes ~1.7 mA into GPIO17 when high. Planned fix: diode (cathode to ESP32) + GPIO17 open-drain.

## Main Flow
  01-arduino-blink/src/blink.c --avr-gcc--> build/blink.elf --avr-objcopy--> build/blink.hex --avrdude/Optiboot--> flash
  reset → main(): setup → sei() → idle loop
  Timer1 hits OCR1A every 2 s → ISR toggles PB5 → LED on/off

## Data and Trust Boundaries
No storage or external services. Hardware: board on /dev/ttyUSB0 via CH340.

## Build and Deployment
Verified on this machine: avrdude 7.1 at /usr/bin/avrdude; user in dialout group.
Verified probe: `avrdude -c arduino -p m328p -P /dev/ttyUSB0 -b 115200` reaches Optiboot 4.4, signature 0x1e950f.
Verified toolchain: avr-gcc 7.3.0, avr-objcopy, avr-size, make (all in /usr/bin); avr-libc headers at /usr/lib/avr/include.

Build (2026-11-06 reorganization: three self-contained project folders 01-arduino-blink, 02-esp32-blink, 03-esp32-arduino-uart; run commands from inside each folder, see its README): `scripts/build.sh [flash]` or `make` / `make flash` / `make clean`; PORT overridable.

## Unknowns
- Clock frequency assumed 16 MHz; not verified (fuses unreadable via Optiboot).
- Physical chip marking not yet confirmed (ATmega328P vs look-alike).

## ESP32 target (added 2026-11-04)
- Board: ESP32-D0WD rev v1.0, 4 MB flash, CP2102 USB bridge. Port number depends on what's plugged in; identify it with /dev/serial/by-id (CP2102 = ESP32, ch340 = AVR).
- Tools: esptool 5.6.0 via pipx; xtensa-esp32-elf-gcc 16.1.0 in ~/.local/opt/xtensa-esp-elf-16.1.0.
- 02-esp32-blink/src/blink.c: polled blink on GPIO2 using the Xtensa CCOUNT cycle counter, 2 s on/off; disables RTC and TIMG0/1 watchdogs; sends '1'/'0' on UART0 each toggle.
- Build/flash: `./build_esp32.sh [flash]` inside 02-esp32-blink (src/$APP.c) or 03-esp32-arduino-uart (esp32/$APP.c, default APP=dashboard); image flashed at 0x1000.
- 03-esp32-arduino-uart/esp32/dashboard.c (implemented 2026-11-05; UART2 RX + async printing verified on hardware, command/table path not yet): text dashboard on UART0 115200; UART2 9600 on GPIO17 TX / GPIO16 RX to the AVR; fetches the AVR table with '?', validates, forwards; prints AVR replies async; 200 ms no-reply warning.
- Verified on hardware: external LED on GPIO2 blinks 2 s on/off. Board has no GPIO-controlled user LED (its LED is serial activity).

## To-do (parked ideas)
- SG90 angle accuracy/jitter (parked 2026-11-05): commanded angle doesn't match the rotation. Suspects: PULSE_MIN/MAX (1-2 ms) narrower than the SG90's travel, USB 5 V sag.
- ESP32 passkey (parked 2026-11-04). Goal: a real FIDO2/WebAuthn passkey that sites accept, KeePassXC-like; a secret on the laptop plus a secret on the ESP combine to produce the credential, using a laptop<->ESP protocol agreed in advance.
  - Known constraint: the ESP32-D0WD has no native USB (CP2102 is serial only), so a laptop-side bridge (virtual FIDO HID via /dev/uhid, or a browser extension) or BLE would be needed.
  - Open questions: where the two secrets combine / who sees the full private key; threat model (stolen ESP, stolen laptop, laptop malware); whether to require a button press on the ESP.
