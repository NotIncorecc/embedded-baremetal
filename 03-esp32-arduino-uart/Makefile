# Build and flash an AVR program with make (default PROG=periph_uart -> avr/periph_uart.c).
#   make          -> build build/periph_uart.hex
#   make PROG=power_test flash -> build and upload avr/power_test.c
#   make flash    -> build (if needed), then upload
#   make clean    -> delete build/
#   PORT=/dev/ttyUSB1 make flash

MCU    := atmega328p
F_CPU  := 16000000UL
PORT   ?= /dev/ttyUSB0
BAUD   := 115200

CC      := avr-gcc
OBJCOPY := avr-objcopy
CFLAGS  := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=gnu99 -Wall -Wextra

PROG ?= periph_uart
SRC  := avr/$(PROG).c
ELF  := build/$(PROG).elf
HEX  := build/$(PROG).hex

.PHONY: all flash clean

all: $(HEX)

# Rebuilt only when the source (or this Makefile) is newer than the ELF
$(ELF): $(SRC) Makefile
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC)
	avr-size --mcu=$(MCU) -C $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -j .text -j .data $< $@

flash: $(HEX)
	avrdude -c arduino -p m328p -P $(PORT) -b $(BAUD) -U flash:w:$(HEX):i

clean:
	rm -rf build
