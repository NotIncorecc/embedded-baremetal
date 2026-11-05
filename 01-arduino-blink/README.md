# 01 – Arduino LED blink (bare-metal AVR)

Blinks the on-board LED of an Uno-style clone (ATmega328P, CH340 USB bridge) in plain C with avr-libc. There is no Arduino core and no `delay()`: a hardware timer does the timing and the CPU only sets things up.

## Architecture

```
 Timer1 (CTC mode, clk/1024)          ISR(TIMER1_COMPA_vect)         PB5 (D13)
 counts 0 .. OCR1A = 31249  ──match──► PORTB ^= (1 << PB5)  ───────► on-board LED
 (15625 ticks/s → 2 s)                 (XOR flips the pin)            2 s on / 2 s off

 main(): configure PB5 as output → configure Timer1 → sei() → empty loop
```

| Piece | What it does |
|---|---|
| `DDRB \|= (1 << DDB5)` | Makes PB5 (Arduino D13, the on-board LED) an output. |
| Timer1, CTC mode | Counts up to `OCR1A` and resets to 0. With a 16 MHz clock and prescaler 1024, 31250 ticks = 2 s, so `OCR1A = 31250 - 1` (the counter starts at 0). |
| Compare-match interrupt | Fires every time the counter hits `OCR1A`. The ISR toggles PB5. |
| Empty `while (1)` | All the work is interrupt-driven, so `main()` has nothing to do after setup. |

The clock is assumed to be 16 MHz (`F_CPU`). The fuses can't be read through the bootloader, so this is unverified.

## Layout

```
01-arduino-blink/
├── src/blink.c         the firmware
├── Makefile            make targets (build / flash / clean)
├── scripts/build.sh    the same steps as plain commands, no make
├── build/              compiler output (.elf, .hex)
└── lessons/blinkled.md the chat this project came from
```

## Requirements

`avr-gcc`, `avr-libc`, `avr-objcopy`/`avr-size`, `make`, `avrdude`, and access to the serial port (user in the `dialout` group). The board is flashed through the Optiboot bootloader at 115200 baud on `/dev/ttyUSB0`.

## Commands (run from this folder)

```bash
# Check the board answers (expect signature 0x1e950f)
avrdude -c arduino -p m328p -P /dev/ttyUSB0 -b 115200

# With make
make              # build build/blink.hex
make flash        # build, then upload
make clean        # delete build/
PORT=/dev/ttyUSB1 make flash     # different port

# Without make
scripts/build.sh          # build only
scripts/build.sh flash    # build and upload
```
