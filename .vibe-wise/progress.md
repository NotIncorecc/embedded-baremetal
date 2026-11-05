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

## Arduino <-> ESP32 communication (started 2026-11-04)
- Learner requirement: wire the AVR clone and the ESP32 together to learn UART, SPI, I2C "and so on".
- Introduced (AI explained): the three protocols at a high level; 5 V AVR vs 3.3 V ESP32 (ESP32 pins not 5 V tolerant); shared ground; AVR's only hardware UART is also used by USB (pins 0/1); ESP32 UART0 is the flash/console port.
- Build checkpoint asked: which protocol first, which board leads, how to handle 5 V vs 3.3 V, how to tell it works.
- Learner answers (2026-11-05): UART first ("simplest"); ESP32 leads "as it has more clock speed"; parts: 10k resistors, 10uF caps, diodes, servo; level shift via a voltage divider; servo on the follower MCU, controlled by commands from the leader.
- Feedback given: UART is peer-to-peer, so "leader" is an application-level role (who sends commands), not about clock speed; servo needs 50 Hz PWM with 1-2 ms pulses; 2x10k divider gives 2.5 V, right at the ESP32 VIH (~2.48 V); AVR pins 0/1 are shared with the CH340.
- Open (asked): divider ratio from 10k parts and which direction(s) need it; how to handle the pins 0/1 sharing; command format; servo power.
- Learner answers (2026-11-05, round 2):
  - Demonstrated understanding: divider = (10k||10k) on top, 10k on bottom, 5 V x 10/15 = 3.33 V. Correct math and correct tap point.
  - Pins 0/1: "disable UART when uploading". Feedback: the conflict is electrical (the ESP32 still drives the wire), and the CH340's 5 V TX sits on pin 0 through ~1k.
  - Command format: ASCII text; the AVR converts the number to PWM (good split of responsibilities). Needs reinforcement: thought the stop bit/idle line ends a message; corrected: the stop bit ends one byte, so message framing needs a delimiter.
  - Verification: log both boards and check the AVR receives the numbers. AI added: the AVR's TX also reaches the laptop via the CH340.
- Open (asked): approach for pin 0 sharing; delimiter + invalid-input handling; does AVR send anything back (decides if a divider is needed); servo power.
- Learner answers (round 3): asked AI to suggest a pin 0 approach; message = fixed length + '\n' terminator; AVR replies to the ESP32 for feedback (so the divider is needed on AVR TX -> ESP32 RX); servo powered from the Arduino 5 V pin.
- AI offered pin 0 options (learner asked): unplug during upload / diode trick / software UART. Flagged the USB 5 V budget for the servo.
- Open (asked): pick a pin 0 option + reason; exact message format + invalid-input behavior; reply contents.
- Learner decisions (round 4): pin 0 option A (unplug the ESP32 TX wire before AVR uploads); diode option B as a later experiment; message format delegated to AI ("include polarity"); servo is an SG90; bad input: leave the servo unchanged, log it, reply Invalid-Input; other failures reply "error"; success = servo moves; asked about "control states", keeping it simple.
- AI explained: SG90 is open-loop (no position feedback), so "OK" can only mean the pulse was updated.
- Step 1 APPROVED ("Implement this step") and implemented 2026-11-05: src/servo_uart.c + Makefile PROG variable. "ddd\n" 000-180, 9600 8N1, replies OK ddd / ERR INVALID / ERR, BOOT banner, 2-state receiver, Timer1 mode 14 on D9, 1-2 ms pulse; no pulses until the first valid command (AI detail).
- Verified: both programs build with no warnings (servo_uart 610 B flash, 28 B RAM).
- HARDWARE VERIFIED by learner 2026-11-05: flashed; servo moves; all replies match the test table (OK / ERR INVALID cases).
- Step 2 Build checkpoint asked: ESP32 command source, reply handling (wait/timeout), pins.
- Learner answers (step 2): wants a mini "dashboard" on the ESP32 for controlling peripherals; commands typed into the dashboard, ESP32 passes them to the AVR; ESP32 does not wait for replies (feedback only); ERR / ERR INVALID shown on the dashboard; asked AI to find free UART pins in the datasheet.
- AI found (datasheet Table 2): U2RXD = GPIO16, U2TXD = GPIO17 (free on WROOM-32); U1 default pins GPIO9/10 are on the internal flash (GPIO6-11); strapping pins GPIO0/2/5/12/15; GPIO34-39 are input-only.
- Open (asked): "dashboard" = serial-terminal text UI (bare-metal OK) or web page over WiFi (needs Espressif's closed WiFi stack via ESP-IDF/Arduino, not bare-metal)?
- Learner decisions: text dashboard on the serial port (UART0); project aim = show as many peripherals as possible; commands must name the peripheral type and the pin it is on.
- AI explained: on the Uno only some pins have hardware PWM, and only Timer1 (D9/D10) gives clean 50 Hz servo timing in hardware.
- Open (asked): exact command grammar, byte by byte; fixed vs variable length; replies for an unknown type or bad pin; protocol change now or after the plain ESP32<->AVR link works.
- Learner decisions: short format "S09090\n" (type letter + 2-digit pin + 3-digit value); LED as "L100" (= 100% output); fixed length per command type ("whatever convention makes it scalable"); change the protocol now; add a second servo now.
- Demonstrated understanding: said the AVR cannot detect what is attached; only the designer knows which peripheral is on which pin (leads to a firmware pin-assignment table).
- Feedback given: "L100" has no pin, which breaks the learner's own rule; LED "%" is a PWM duty cycle, not a voltage level; D13 has no PWM.
- Step 2a APPROVED ("Implement this step") and implemented 2026-11-05: src/servo_uart.c renamed to src/periph_uart.c. Protocol v2: S<pin><angle>, L<pin><pct>; cmd_types[] + outputs[] tables; D9/D10 servos (Timer1 OC1A/OC1B), D6 LED (Timer0 OC0A ~977 Hz; 0 % disconnects PWM, drives low); replies echo the command.
- Verified: AVR build has no warnings (890 B flash, 48 B RAM). A throwaway host test in the scratchpad (stub registers) ran 16 commands: all expected OK/INVALID, register values correct, rejected commands changed nothing. NOT yet flashed or tested on hardware.
- HARDWARE VERIFIED by learner 2026-11-05: protocol v2 works. Reported: the SG90 is sometimes buggy and the angle doesn't match the actual rotation; learner PARKED that ("fix later"). AI note: the 1-2 ms pulse limits give less than the full 180 degrees of travel (known proposal tradeoff).
- Step 2b Build checkpoint asked: ESP32 validates or forwards raw; per-character vs line forwarding/echo; how replies are shown; behaviour when the AVR is silent.
- Learner decisions (step 2b): the ESP32 validates commands before sending, the AVR still parses; line-buffered, sent on Enter; simple numbered menu (1 = view peripherals, 2 = write a command, ...); a dead AVR must be visible, but the ESP32 doesn't block on the reply before the next command (detect, don't wait).
- Open (asked): validating on both boards duplicates the pin table; where the peripheral list comes from (ESP32 copy vs asking the AVR); what the ESP32 checks vs the AVR.
- Learner decision: the AVR owns the table and sends it to the ESP32 at startup and whenever menu option 1 is chosen; the ESP32 then validates against it (single source of truth).
- Feedback given: contradicts the learner's earlier point that the AVR can't detect hardware; physically removing a servo won't change the table, only a reflash does. A table sent only at AVR startup is missed if the ESP32 boots later, so the ESP32 needs to be able to request it.
- Step 2b APPROVED ("Implement this step") and implemented 2026-11-05: AVR '?' -> "T S09 180" rows + "T END"; src/esp32/dashboard.c (menu 1/2, line input + backspace, validation against the fetched table, async "AVR: " lines, 200 ms no-reply warning, re-query on BOOT, UART0 clkdiv sanity print); scripts/build_esp32.sh takes APP=.
- Verified: AVR build 1184 B, no warnings; ESP32 dashboard 2493 B, no warnings, no .data/.bss, no undefined symbols; blink still builds. Throwaway host simulations (scratchpad) passed: AVR table reply, 16 parser cases, dashboard flows (no table, timeout, option 1, backspace, 5 rejection reasons, BOOT re-query + noise bytes, reply while typing). Register addresses checked against ESP-IDF v5.4 headers. NOT yet run on hardware; two-board wiring not yet built.
- Learner question (2026-11-05): power the AVR from the ESP32 board's 5V pin instead of the AVR's own USB, so the link "is convincing". Learner said two wires connect the boards (TX/RX); GND was missing. AI explained: ESP32 VIN/5V = USB 5 V (often via a diode); feed the AVR's 5V pin (not VIN, not 3V3); a shared USB budget with the servos risks brownouts; the GND wire is required; the CH340 stays powered from the AVR rail either way. Learner's power choice not yet made.
- Next: learner flashes and runs the laptop terminal test; then Step 2 checkpoint (ESP32 UART2 + board-to-board wiring with divider).

## ESP32 passkey project (PARKED by learner 2026-11-04: "a bit overwhelming now"; see project-map.md To-do)
- Introduced (AI explained): password-typer vs real FIDO2/WebAuthn passkey; classic ESP32 has no native USB (CP2102 is serial only), so it can't be a USB HID device; BLE / other chip / laptop helper are the routes; flash can be dumped with esptool.
- Learner requirements: a real FIDO2 passkey that sites accept, "like KeePassXC"; split secret: laptop key + ESP key combine into the credential; laptop<->ESP protocol agreed in advance.
- Open questions when resumed (Build checkpoint, unanswered): where the two secrets get combined and who sees the final private key; how the browser reaches the ESP; threat model (stolen ESP / stolen laptop / laptop malware). No design confirmed, no code.
