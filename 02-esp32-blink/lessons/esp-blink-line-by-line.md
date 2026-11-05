Analyzed blink.c:L1-79
Analyzed blink.c:L1-40

This code in [blink.c](file:///home/gokulganesan/ardunio-baremetal/src/esp32/blink.c) is a **bare-metal C program for the ESP32** (Xtensa architecture). It runs without an operating system (no FreeRTOS), without the ESP-IDF framework, and without the C standard library. Instead, it interacts directly with the ESP32’s memory-mapped hardware registers.

Below is the line-by-line breakdown of how it works.

---

### 1. Header & Register Access Macro

```c
1: /* Bare-metal ESP32 blink: polled (no interrupts), no ESP-IDF, no libc.
2:  * Register addresses/offsets checked against ESP-IDF v5.4 soc headers. */
3: #include <stdint.h>
```
* **Lines 1–2**: Informational comment explaining that this code runs completely bare-metal using polled delays and direct register offsets.
* **Line 3**: Includes the C standard header for fixed-width integers (e.g., `uint32_t`).

```c
5: #define REG(addr) (*(volatile uint32_t *)(addr))
```
* **Line 5**: Defines a macro to read or write a 32-bit hardware register at memory address `addr`.
  * `(volatile uint32_t *)(addr)` casts the integer address to a pointer to a volatile 32-bit unsigned integer.
  * The `volatile` keyword prevents the compiler from optimizing out repeated reads/writes or caching the values in CPU registers, ensuring every read/write hits hardware memory directly.
  * Dereferencing `*` accesses the value stored at that address.

---

### 2. GPIO & Pin Routing Definitions

```c
8: #define LED_GPIO            2u                          /* first guess; board schematic unknown */
```
* **Line 8**: Sets the target pin number to GPIO 2 (`2u` is unsigned integer 2).

```c
9: #define GPIO_BASE           0x3FF44000u
10: #define GPIO_OUT_W1TS_REG   (GPIO_BASE + 0x0008u)       /* write 1 -> pin high */
11: #define GPIO_OUT_W1TC_REG   (GPIO_BASE + 0x000Cu)       /* write 1 -> pin low  */
12: #define GPIO_ENABLE_W1TS_REG (GPIO_BASE + 0x0024u)      /* write 1 -> output enable */
```
* **Line 9**: Base memory address in ESP32 physical address space for the GPIO controller (`0x3FF44000`).
* **Line 10**: **W1TS** (Write 1 to Set) register. Writing a bit `1` at position $n$ drives GPIO $n$ **HIGH**. Writing `0` leaves the pin untouched. This allows atomic bit updates without read-modify-write race conditions.
* **Line 11**: **W1TC** (Write 1 to Clear) register. Writing a bit `1` at position $n$ drives GPIO $n$ **LOW**.
* **Line 12**: Output Enable Set register. Writing `1` at bit $n$ configures GPIO $n$ as an **output** pin.

```c
13: #define GPIO_FUNC_OUT_SEL_CFG_REG(n) (GPIO_BASE + 0x0530u + 4u * (n))
14: #define SIG_GPIO_OUT_IDX    256u                        /* "plain GPIO output" signal */
```
* **Line 13**: Calculates the GPIO Matrix output routing register for pin `n`. In the ESP32, internal signals are connected to physical pins through a crossbar matrix.
* **Line 14**: Matrix signal index `256` represents direct/simple software GPIO control (bypassing special peripheral functions like SPI/I2C/UART).

```c
15: #define IO_MUX_GPIO2_REG    0x3FF49040u
16: #define MCU_SEL_SHIFT       12u
17: #define MCU_SEL_MASK        (0x7u << MCU_SEL_SHIFT)
18: #define PIN_FUNC_GPIO       2u
```
* **Line 15**: Address of the IO_MUX configuration register specifically for physical pad GPIO 2 (`0x3FF49040`).
* **Lines 16–17**: The `MCU_SEL` bitfield (bits 12..14) selects the function of the physical pad.
* **Line 18**: Function index `2` configures this pad to function as standard GPIO.

---

### 3. Watchdog Timer & UART Definitions

```c
21: #define WDT_WKEY            0x50D83AA1u
22: #define RTC_CNTL_BASE       0x3FF48000u
23: #define RTC_WDTCONFIG0_REG  (RTC_CNTL_BASE + 0x8Cu)
24: #define RTC_WDTWPROTECT_REG (RTC_CNTL_BASE + 0xA4u)
25: #define TIMG0_BASE          0x3FF5F000u
26: #define TIMG1_BASE          0x3FF60000u
27: #define TIMG_WDTCONFIG0(b)  ((b) + 0x48u)
28: #define TIMG_WDTWPROTECT(b) ((b) + 0x64u)
```
* **Line 21**: Magic 32-bit unlock key required by the ESP32 hardware to disable or modify watchdog timers.
* **Lines 22–28**: Base addresses and register offsets for the three built-in hardware watchdog timers: the RTC watchdog and the two Timer Group watchdogs (TIMG0 and TIMG1). When booting bare-metal, the ROM leaves these armed; if not disabled, they will reboot the chip.

```c
31: #define UART0_FIFO_REG      0x3FF40000u                 /* write a byte -> transmit */
```
* **Line 31**: Transmit FIFO data register for UART0. Writing an ASCII byte here sends it out over serial (already configured at 115200 baud by the ROM bootloader).

---

### 4. Clock and Delay Calculation

```c
35: #define CPU_HZ              40000000u
36: #define BLINK_CYCLES        (2u * CPU_HZ)               /* 2 s on, 2 s off */
```
* **Line 35**: Defines the assumed CPU frequency: 40 MHz (crystal oscillator speed at reset before any PLL multiplies it).
* **Line 36**: Calculates 2 seconds worth of CPU clock cycles: $2 \times 40{,}000{,}000 = 80{,}000{,}000$ cycles.

---

### 5. Low-Level Helper Functions

```c
38: static inline __attribute__((always_inline)) uint32_t ccount(void)
39: {
40:     uint32_t c;
41:     __asm__ volatile("rsr %0, ccount" : "=a"(c));       /* Xtensa cycle counter */
42:     return c;
43: }
```
* **Lines 38–43**: Reads the Xtensa CPU’s internal cycle counter register (`ccount`) using inline assembly (`rsr` = Read Special Register). This hardware register increments on every single clock tick.

```c
45: static inline __attribute__((always_inline)) void wait_cycles(uint32_t n)
46: {
47:     uint32_t start = ccount();
48:     while ((uint32_t)(ccount() - start) < n) { }        /* unsigned math survives wraparound */
49: }
```
* **Lines 45–49**: A cycle-accurate busy-wait function.
  * Records the start count.
  * Loops until the difference reaches `n`.
  * Using unsigned subtraction `(uint32_t)(ccount() - start)` correctly handles 32-bit overflow wraparound seamlessly.

```c
51: static inline __attribute__((always_inline)) void disable_wdt(uint32_t config, uint32_t protect)
52: {
53:     REG(protect) = WDT_WKEY;                            /* unlock writes */
54:     REG(config)  = 0;                                   /* clear enable bit (and all other fields) */
55:     REG(protect) = 0;                                   /* lock again */
56: }
```
* **Lines 51–56**: Unlocks the watchdog configuration by writing `WDT_WKEY`, disables the timer by clearing the register to `0`, and re-locks write protection.

---

### 6. Program Entry Point (`call_start_cpu0`)

```c
58: void call_start_cpu0(void) __attribute__((section(".text.call_start_cpu0")));
59: 
60: void call_start_cpu0(void)
61: {
```
* **Lines 58–61**: Declares and defines the primary core (CPU 0) entry point.
  * `__attribute__((section(".text.call_start_cpu0")))` places this function in a dedicated ELF section so the linker script can position it at the exact memory address executed immediately after the first-stage bootloader.

```c
62:     disable_wdt(RTC_WDTCONFIG0_REG, RTC_WDTWPROTECT_REG);
63:     disable_wdt(TIMG_WDTCONFIG0(TIMG0_BASE), TIMG_WDTWPROTECT(TIMG0_BASE));
64:     disable_wdt(TIMG_WDTCONFIG0(TIMG1_BASE), TIMG_WDTWPROTECT(TIMG1_BASE));
```
* **Lines 62–64**: Disables all three watchdog timers so they do not reset the board during execution.

```c
67:     REG(IO_MUX_GPIO2_REG) = (REG(IO_MUX_GPIO2_REG) & ~MCU_SEL_MASK) | (PIN_FUNC_GPIO << MCU_SEL_SHIFT);
```
* **Line 67**: Configures the IO MUX pad for GPIO 2: clears the 3 `MCU_SEL` bits and writes `PIN_FUNC_GPIO` (function 2), routing physical pad 2 to the GPIO matrix.

```c
68:     REG(GPIO_FUNC_OUT_SEL_CFG_REG(LED_GPIO)) = SIG_GPIO_OUT_IDX;
```
* **Line 68**: Connects GPIO 2’s output driver to the direct GPIO output register (signal `256`).

```c
69:     REG(GPIO_ENABLE_W1TS_REG) = 1u << LED_GPIO;
```
* **Line 69**: Sets bit 2 in the output enable register, setting GPIO 2 as an **OUTPUT** (equivalent to `pinMode(2, OUTPUT)` in Arduino).

---

### 7. Main Blink Loop

```c
71:     for (;;) {
72:         REG(GPIO_OUT_W1TS_REG) = 1u << LED_GPIO;
73:         REG(UART0_FIFO_REG) = '1';                      /* heartbeat: pin went high */
74:         wait_cycles(BLINK_CYCLES);
75:         REG(GPIO_OUT_W1TC_REG) = 1u << LED_GPIO;
76:         REG(UART0_FIFO_REG) = '0';                      /* heartbeat: pin went low */
77:         wait_cycles(BLINK_CYCLES);
78:     }
79: }
```
* **Line 71**: Enters an infinite execution loop.
* **Line 72**: Writes `1 << 2` to `GPIO_OUT_W1TS_REG`. Pin 2 goes **HIGH** (external LED turns ON).
* **Line 73**: Sends ASCII `'1'` over UART0 for serial monitor debugging.
* **Line 74**: Waits for 2 seconds (80 million CPU cycles).
* **Line 75**: Writes `1 << 2` to `GPIO_OUT_W1TC_REG`. Pin 2 goes **LOW** (external LED turns OFF).
* **Line 76**: Sends ASCII `'0'` over UART0.
* **Line 77**: Waits another 2 seconds before the loop repeats.