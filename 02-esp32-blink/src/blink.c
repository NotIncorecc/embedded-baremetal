/* Bare-metal ESP32 blink: polled (no interrupts), no ESP-IDF, no libc.
 * Register addresses/offsets checked against ESP-IDF v5.4 soc headers. */
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

/* --- GPIO / IO_MUX --- */
#define LED_GPIO            2u                          /* first guess; board schematic unknown */
#define GPIO_BASE           0x3FF44000u
#define GPIO_OUT_W1TS_REG   (GPIO_BASE + 0x0008u)       /* write 1 -> pin high */
#define GPIO_OUT_W1TC_REG   (GPIO_BASE + 0x000Cu)       /* write 1 -> pin low  */
#define GPIO_ENABLE_W1TS_REG (GPIO_BASE + 0x0024u)      /* write 1 -> output enable */
#define GPIO_FUNC_OUT_SEL_CFG_REG(n) (GPIO_BASE + 0x0530u + 4u * (n))
#define SIG_GPIO_OUT_IDX    256u                        /* "plain GPIO output" signal */
#define IO_MUX_GPIO2_REG    0x3FF49040u
#define MCU_SEL_SHIFT       12u
#define MCU_SEL_MASK        (0x7u << MCU_SEL_SHIFT)
#define PIN_FUNC_GPIO       2u

/* --- Watchdogs (ROM leaves them running; they would reset the chip) --- */
#define WDT_WKEY            0x50D83AA1u
#define RTC_CNTL_BASE       0x3FF48000u
#define RTC_WDTCONFIG0_REG  (RTC_CNTL_BASE + 0x8Cu)
#define RTC_WDTWPROTECT_REG (RTC_CNTL_BASE + 0xA4u)
#define TIMG0_BASE          0x3FF5F000u
#define TIMG1_BASE          0x3FF60000u
#define TIMG_WDTCONFIG0(b)  ((b) + 0x48u)
#define TIMG_WDTWPROTECT(b) ((b) + 0x64u)

/* --- UART0 (ROM already configured 115200 baud for its boot banner) --- */
#define UART0_FIFO_REG      0x3FF40000u                 /* write a byte -> transmit */

/* CPU clock after ROM boot is assumed to be the 40 MHz crystal. Unverified:
 * if the LED period looks wrong, this is the number to correct. */
#define CPU_HZ              40000000u
#define BLINK_CYCLES        (2u * CPU_HZ)               /* 2 s on, 2 s off */

static inline __attribute__((always_inline)) uint32_t ccount(void)
{
    uint32_t c;
    __asm__ volatile("rsr %0, ccount" : "=a"(c));       /* Xtensa cycle counter */
    return c;
}

static inline __attribute__((always_inline)) void wait_cycles(uint32_t n)
{
    uint32_t start = ccount();
    while ((uint32_t)(ccount() - start) < n) { }        /* unsigned math survives wraparound */
}

static inline __attribute__((always_inline)) void disable_wdt(uint32_t config, uint32_t protect)
{
    REG(protect) = WDT_WKEY;                            /* unlock writes */
    REG(config)  = 0;                                   /* clear enable bit (and all other fields) */
    REG(protect) = 0;                                   /* lock again */
}

void call_start_cpu0(void) __attribute__((section(".text.call_start_cpu0")));

void call_start_cpu0(void)
{
    disable_wdt(RTC_WDTCONFIG0_REG, RTC_WDTWPROTECT_REG);
    disable_wdt(TIMG_WDTCONFIG0(TIMG0_BASE), TIMG_WDTWPROTECT(TIMG0_BASE));
    disable_wdt(TIMG_WDTCONFIG0(TIMG1_BASE), TIMG_WDTWPROTECT(TIMG1_BASE));

    /* Route the pad to the GPIO matrix, drive it as a plain output. */
    REG(IO_MUX_GPIO2_REG) = (REG(IO_MUX_GPIO2_REG) & ~MCU_SEL_MASK) | (PIN_FUNC_GPIO << MCU_SEL_SHIFT);
    REG(GPIO_FUNC_OUT_SEL_CFG_REG(LED_GPIO)) = SIG_GPIO_OUT_IDX;
    REG(GPIO_ENABLE_W1TS_REG) = 1u << LED_GPIO;

    for (;;) {
        REG(GPIO_OUT_W1TS_REG) = 1u << LED_GPIO;
        REG(UART0_FIFO_REG) = '1';                      /* heartbeat: pin went high */
        wait_cycles(BLINK_CYCLES);
        REG(GPIO_OUT_W1TC_REG) = 1u << LED_GPIO;
        REG(UART0_FIFO_REG) = '0';                      /* heartbeat: pin went low */
        wait_cycles(BLINK_CYCLES);
    }
}
