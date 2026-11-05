# Learner Profile

Learning mode: active
Onboarding: complete

## Project
Situation: New
Building: Bare-metal firmware for a CH340-based Uno-style clone (ATmega328P, Optiboot @115200, /dev/ttyUSB0). First task: blink the on-board LED in C using avr-libc.
Codebase familiarity: Not applicable (new project)
Learning scope: Not specified

## Experience
Overall programming: Intermediate (self-reported)
Stack familiarity: AVR embedded C (registers, avr-gcc/avr-libc, avrdude): Beginner (self-reported)

## Goals
Primary: Learn communication protocols (UART done; SPI, I2C next) by linking the AVR clone and the ESP32, bare-metal on both.
Capability goal: Drive as many peripherals as possible from an ESP32 text dashboard.

## Preferences
Checkpoint frequency: Normal
Question style: Open-ended
Implementation style: AI writes code

## Strong Concepts
- AVR GPIO bit set/clear (read-modify-write).
- Voltage divider design (picked 10k||10k over 10k for 3.33 V, correct tap point).
- Peripheral discovery: the MCU can't detect what's wired; the designer declares it (a table on the AVR as the single source of truth).
- Test isolation: wanted the power supply tested separately from the comms (own self-test program, worst case watched via BOOT).

## Developing Concepts
- UART framing: stop bit = end of one byte, not of a message (corrected; uses '\n' now).
- PWM: duty cycle vs "output voltage"; which pins/timers can do hardware PWM.
- Roles in UART: leader/follower is an application choice, not a matter of clock speed.
- Electrical interfacing: shared pins (CH340 on D0), back-current into 3.3 V pins, open-drain.

## Revisit
- Memory: flash vs EEPROM (earlier confusion).
- CTC off-by-one (OCR = N - 1).
- Polling vs interrupt tradeoff (reasons given so far are partial).
- ESP32 specifics not yet assessed: watchdogs, GPIO matrix, ROM boot flow.
