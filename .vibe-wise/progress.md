# Learning Progress

## Blink v1 (implemented 2026-11-04)
- Learner approved "Implement this step" for: Timer1 CTC, /1024, OCR1A 31249, ISR toggle, Make vs shell script.
- Result: builds clean both ways (210 bytes), identical HEX, flashed and verified by avrdude. Visual LED check by learner pending.
- Unresolved: learner's reason for hardware timer over busy-wait (asked twice, not answered).

## AVR GPIO registers
- Introduced (AI explained): on-board LED is PB5; DDRx = direction, PORTx = output level; registers are memory-mapped and named by avr/io.h.
- Demonstrated understanding: learner independently chose DDRB bit 5 = 1 to make the LED pin an output.
- Demonstrated understanding: gave read-modify-write bit set (x | (1<<5)) and clear (x & ~(1<<5)) correctly; said learned elsewhere.
- Needs reinforcement: confused Arduino pin label "13" with a port/bit ("PORT 13"); corrected to PORTB bit 5.

## Timer1 registers
- Introduced (AI explained, on learner request): naming scheme (TCCR/TCNT/OCR/TIMSK/TIFR, CS, WGM); every register in blink.c incl. implicit TIFR1/OCF1A and SREG I-bit via sei(); TCCR1B bit layout, value 0x0D, WGM split across TCCR1A/B (mode 4 CTC), CS table, why TCCR1B is written last.
- Learner called registers "resistors"; terminology corrected.
- Demonstrated understanding: not yet assessed.

## Timing on AVR
- Introduced (AI explained): no API reports clock speed; F_CPU is declared to the compiler. Options: busy-wait (_delay_ms) vs hardware timer.
- Learner reasoning: proposed using the board's clock for timing. Computed 16 MHz / 1024 = 15625 ticks/s and 2 s = 31250 ticks, checked it fits 16 bits. Chose interrupt over polling because polling "hogs the CPU".
- Needs reinforcement: CTC off-by-one (counter counts 0..OCR1A, so OCR1A = N - 1). Initial stated reason for timer ("lag when code starts") was inaccurate; not clarified.

## Memory and flashing
- Introduced (AI explained): program goes to 32 KB flash, not EEPROM; SRAM 2 KB, EEPROM 1 KB. Pipeline: avr-gcc → ELF → avr-objcopy → HEX → avrdude → Optiboot → flash.
- Needs reinforcement: learner initially said code is flashed to EEPROM.

## ESP32 blink (2026-11-04, switched target from AVR clone to ESP32-WROOM dev board)
- Learner decisions: poll instead of interrupt (reasoned that the ESP32 may not match the AVR design and interrupts cost more setup); GPIO 2 as first LED guess (from an outside LLM search; not verified for this board); approved "Implement this step" for blink, toolchain download, and flash at 0x1000.
- Learner correctly said the board datasheet is where to look for the LED pin; datasheet is for the module only and has no LED info. Board silkscreen has no model name. No multimeter available.
- Built: src/esp32/blink.c, esp32.ld, scripts/build_esp32.sh. Flashed at 0x1000 (overwrites original bootloader). ROM accepted image (entry 0x40080034).
- Serial heartbeat (UART0 FIFO) shows toggles every ~2.0 s: code runs, 40 MHz CPU clock assumption is correct.
- Result: learner sees no LED blinking. Open question: which GPIO drives a user LED, or whether the board has one at all.
- Unanswered: learner has not yet said whether any LED other than power is present; not yet reasoned about which assumption (pin vs. clock vs. code) failed.
- RESULT (2026-11-04): external LED + resistor on GPIO2 blinks ~2 s on / ~2 s off. Confirms pin setup, watchdog handling, polled CCOUNT timing, and 40 MHz CPU clock assumption on hardware. Board's own LED lights during flashing only: likely a USB-serial activity LED, not a GPIO. First ESP32 milestone done.
- Not yet assessed: learner's understanding of ESP32-specific pieces (watchdogs, IO_MUX/GPIO matrix, flashing at 0x1000, ROM boot flow). Learner never answered how they'd decide polling vs interrupt beyond "simpler".
