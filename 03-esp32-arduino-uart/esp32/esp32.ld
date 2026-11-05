/* Minimal linker script: ESP32 ROM loads this image from flash 0x1000
 * (as if it were the 2nd-stage bootloader) and jumps to the ELF entry point.
 * Code runs from internal IRAM; no cache, no flash-mapped code. */

ENTRY(call_start_cpu0)

MEMORY
{
    iram : ORIGIN = 0x40080000, LENGTH = 0x20000
    dram : ORIGIN = 0x3FFB0000, LENGTH = 0x2C000
}

SECTIONS
{
    .iram.text : ALIGN(4)
    {
        KEEP(*(.text.call_start_cpu0))
        *(.literal .literal.*)
        *(.text .text.*)
    } > iram

    /* IRAM can't be read byte-wise, so constants go to DRAM. */
    .dram.rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
    } > dram
}
