# Build and flash src/blink.c with make.
#   make          -> build build/blink.hex
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

SRC := src/blink.c
ELF := build/blink.elf
HEX := build/blink.hex

.PHONY: all flash clean

all: $(HEX)

# Rebuilt only when blink.c (or this Makefile) is newer than the ELF
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
