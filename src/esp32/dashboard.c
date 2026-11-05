/* Bare-metal ESP32 text dashboard: no ESP-IDF, no libc, polled (no interrupts).
 *
 *   you (serial terminal, UART0 115200) <--> ESP32 <--> UART2 9600 8N1 <--> AVR
 *                                         GPIO17 = TX2 -> AVR D0
 *                                         GPIO16 = RX2 <- divider <- AVR D1
 *
 * - The AVR owns the peripheral table. The ESP32 asks for it with "?" at
 *   startup, after seeing "BOOT", and on menu option 1, then validates
 *   commands against it before sending.
 * - Replies from the AVR are printed as "AVR: ..." whenever they arrive.
 * - The ESP32 never waits for a reply; if none arrives within 200 ms it
 *   prints "AVR: no reply".
 *
 * All state lives on the stack: the linker script has no .data/.bss.
 * Register addresses checked against ESP-IDF v5.4 soc headers. */
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

/* --- Peripheral clock / reset (DPORT) --- */
#define DPORT_PERIP_CLK_EN_REG  0x3FF000C0u
#define DPORT_PERIP_RST_EN_REG  0x3FF000C4u
#define DPORT_UART2_BIT         (1u << 23)
#define DPORT_UART_MEM_BIT      (1u << 24)

/* --- UART --- */
#define UART0_BASE              0x3FF40000u
#define UART2_BASE              0x3FF6E000u
#define UART0_FIFO_AHB          0x60000000u     /* FIFO reads go through the AHB alias (as ESP-IDF does) */
#define UART2_FIFO_AHB          0x6002E000u
#define UART_FIFO(b)            (b)
#define UART_CLKDIV(b)          ((b) + 0x14u)
#define UART_STATUS(b)          ((b) + 0x1Cu)
#define UART_CONF0(b)           ((b) + 0x20u)
#define RXFIFO_CNT(st)          ((st) & 0xFFu)
#define TXFIFO_CNT(st)          (((st) >> 16) & 0xFFu)
#define CONF0_PARITY_BITS       0x3u            /* parity_en | parity */
#define CONF0_BIT_NUM_SHIFT     2u              /* 3 -> 8 data bits */
#define CONF0_STOP_SHIFT        4u              /* 1 -> 1 stop bit */
#define CONF0_ERR_WR_MASK       (1u << 26)      /* drop bytes with framing/parity errors */

#define APB_HZ                  40000000u       /* 40 MHz XTAL after ROM boot (confirmed by blink timing) */
#define UART2_BAUD              9600u
/* CLKDIV holds APB/baud as 20-bit integer + 4-bit fraction (1/16ths).
 * 40 MHz / 9600 = 4166.67 -> integer 4166, fraction 11/16. */
#define UART2_DIV16             ((APB_HZ * 16u + UART2_BAUD / 2u) / UART2_BAUD)
#define UART2_CLKDIV_VALUE      (((UART2_DIV16 & 0xFu) << 20) | (UART2_DIV16 >> 4))

/* --- GPIO matrix / IO_MUX --- */
#define GPIO_BASE               0x3FF44000u
#define GPIO_ENABLE_W1TS_REG    (GPIO_BASE + 0x0024u)
#define GPIO_FUNC_IN_SEL_CFG(sig)  (GPIO_BASE + 0x0130u + 4u * (sig))
#define GPIO_FUNC_OUT_SEL_CFG(n)   (GPIO_BASE + 0x0530u + 4u * (n))
#define GPIO_SIG_IN_SEL         (1u << 7)       /* input goes through the matrix */
#define GPIO_OEN_SEL            (1u << 10)      /* output enable from GPIO_ENABLE reg */
#define U2_SIGNAL_IDX           198u            /* U2TXD_OUT_IDX == U2RXD_IN_IDX == 198 */
#define UART2_TX_GPIO           17u
#define UART2_RX_GPIO           16u
#define IO_MUX_GPIO16_REG       0x3FF4904Cu
#define IO_MUX_GPIO17_REG       0x3FF49050u
#define MCU_SEL_SHIFT           12u
#define MCU_SEL_MASK            (0x7u << MCU_SEL_SHIFT)
#define PIN_FUNC_GPIO           2u
#define FUN_IE                  (1u << 9)       /* input enable */

/* --- Watchdogs (ROM leaves them running) --- */
#define WDT_WKEY                0x50D83AA1u
#define RTC_WDTCONFIG0_REG      0x3FF4808Cu
#define RTC_WDTWPROTECT_REG     0x3FF480A4u
#define TIMG0_BASE              0x3FF5F000u
#define TIMG1_BASE              0x3FF60000u
#define TIMG_WDTCONFIG0(b)      ((b) + 0x48u)
#define TIMG_WDTWPROTECT(b)     ((b) + 0x64u)

#define CPU_HZ                  40000000u
#define REPLY_TIMEOUT_CYCLES    (CPU_HZ / 5u)   /* 200 ms */

/* --- Protocol limits --- */
#define MAX_ROWS                16u
#define AVR_LINE_MAX            32u
#define CMD_MAX                 15u
#define PIN_DIGITS              2u

/* ---------- Low-level helpers ---------- */

static inline __attribute__((always_inline)) uint32_t ccount(void)
{
    uint32_t c;
    __asm__ volatile("rsr %0, ccount" : "=a"(c));
    return c;
}

static void disable_wdt(uint32_t config, uint32_t protect)
{
    REG(protect) = WDT_WKEY;
    REG(config)  = 0;
    REG(protect) = 0;
}

static void uart_tx(uint32_t base, char c)
{
    while (TXFIFO_CNT(REG(UART_STATUS(base))) >= 126u) { }   /* FIFO holds 128 */
    REG(UART_FIFO(base)) = (uint8_t)c;
}

/* Returns the next received byte, or -1 if the RX FIFO is empty. */
static int uart_rx(uint32_t base, uint32_t fifo_ahb)
{
    if (RXFIFO_CNT(REG(UART_STATUS(base))) == 0u) {
        return -1;
    }
    return (int)(REG(fifo_ahb) & 0xFFu);
}

/* Console (UART0) output; '\n' becomes "\r\n" for the terminal. */
static void con_putc(char c)
{
    if (c == '\n') {
        uart_tx(UART0_BASE, '\r');
    }
    uart_tx(UART0_BASE, c);
}

static void con_puts(const char *s)
{
    while (*s) {
        con_putc(*s++);
    }
}

static void con_putn(const char *s, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        con_putc(s[i]);
    }
}

/* Zero-padded to `digits` (0 = no padding). */
static void con_num(uint32_t v, uint8_t digits)
{
    char d[10];
    uint8_t n = 0;
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < sizeof d);
    while (n < digits && n < sizeof d) {
        d[n++] = '0';
    }
    while (n) {
        con_putc(d[--n]);
    }
}

static void avr_puts(const char *s)
{
    while (*s) {
        uart_tx(UART2_BASE, *s++);
    }
}

static int8_t parse_digits(const char *s, uint8_t n, uint16_t *out)
{
    uint16_t v = 0;
    if (n == 0) {
        return -1;
    }
    for (uint8_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = (uint16_t)(v * 10u + (uint16_t)(s[i] - '0'));
    }
    *out = v;
    return 0;
}

static uint8_t str_eq(const char *a, uint8_t n, const char *b)
{
    uint8_t i = 0;
    for (; i < n; i++) {
        if (b[i] != a[i]) {
            return 0;
        }
    }
    return b[i] == '\0';
}

/* ---------- Dashboard state ---------- */

struct row {
    char     letter;
    uint8_t  pin;
    uint8_t  digits;                /* value digits = width of the max field */
    uint16_t max;
};

enum mode { MENU, CMD_ENTRY };

struct dash {
    struct row rows[MAX_ROWS];
    uint8_t  nrows;
    uint8_t  table_ok;              /* "T END" received for the latest query */
    uint8_t  show_table;            /* print the table when it completes (option 1) */
    char     avr[AVR_LINE_MAX];
    uint8_t  avr_len;
    uint8_t  avr_overflow;
    char     cmd[CMD_MAX];
    uint8_t  cmd_len;
    enum mode mode;
    uint8_t  waiting;               /* a reply is expected (for the timeout only) */
    uint32_t sent_at;
};

static void print_menu(void)
{
    con_puts("\n=== ESP32 dashboard ===\n1) View peripherals\n2) Send command\n> ");
}

/* After async output, redraw what the user was in the middle of. */
static void redraw_prompt(const struct dash *d)
{
    if (d->mode == CMD_ENTRY) {
        con_puts("cmd> ");
        con_putn(d->cmd, d->cmd_len);
    } else {
        con_puts("> ");
    }
}

static void expect_reply(struct dash *d)
{
    d->waiting = 1;
    d->sent_at = ccount();
}

static void send_query(struct dash *d, uint8_t show)
{
    d->nrows = 0;
    d->table_ok = 0;
    d->show_table = show;
    avr_puts("?\n");
    expect_reply(d);
}

static void print_table(const struct dash *d)
{
    con_puts("\nperipherals reported by AVR:\n");
    if (d->nrows == 0) {
        con_puts("  (none)\n");
    }
    for (uint8_t i = 0; i < d->nrows; i++) {
        const struct row *r = &d->rows[i];
        con_puts("  ");
        con_putc(r->letter);
        con_puts(" on pin ");
        con_num(r->pin, PIN_DIGITS);
        con_puts(": ");
        con_num(0, r->digits);
        con_putc('-');
        con_num(r->max, r->digits);
        con_puts("   e.g. ");
        con_putc(r->letter);
        con_num(r->pin, PIN_DIGITS);
        con_num(r->max, r->digits);
        con_putc('\n');
    }
}

/* "T S09 180" -> row. Malformed rows are ignored. */
static void add_row(struct dash *d, const char *s, uint8_t n)
{
    uint16_t pin, max;
    if (n < 8 || s[5] != ' ' || d->nrows >= MAX_ROWS ||
        parse_digits(s + 3, PIN_DIGITS, &pin) || parse_digits(s + 6, (uint8_t)(n - 6), &max)) {
        return;
    }
    struct row *r = &d->rows[d->nrows++];
    r->letter = s[2];
    r->pin = (uint8_t)pin;
    r->digits = (uint8_t)(n - 6);
    r->max = max;
}

static void handle_avr_line(struct dash *d, const char *s, uint8_t n)
{
    d->waiting = 0;                 /* any complete line counts as "AVR is alive" */

    if (n >= 2 && s[0] == 'T' && s[1] == ' ') {
        if (str_eq(s, n, "T END")) {
            d->table_ok = 1;
            if (d->show_table) {
                d->show_table = 0;
                print_table(d);
                print_menu();
            }
        } else {
            add_row(d, s, n);
        }
        return;
    }

    con_puts("\nAVR: ");
    con_putn(s, n);
    con_putc('\n');
    if (str_eq(s, n, "BOOT")) {
        send_query(d, 0);           /* AVR restarted: its table may have changed */
    }
    redraw_prompt(d);
}

static void avr_byte(struct dash *d, char c)
{
    if (c == '\n') {
        if (d->avr_overflow) {
            con_puts("\nAVR: (line too long, dropped)\n");
            redraw_prompt(d);
        } else {
            handle_avr_line(d, d->avr, d->avr_len);
        }
        d->avr_len = 0;
        d->avr_overflow = 0;
    } else if (c >= 0x20 && c <= 0x7E) {    /* drop '\r' and line noise */
        if (d->avr_len < AVR_LINE_MAX) {
            d->avr[d->avr_len++] = c;
        } else {
            d->avr_overflow = 1;
        }
    }
}

/* Returns 0 if the command is valid against the AVR's table, else prints why. */
static int8_t validate(const struct dash *d, const char *c, uint8_t n)
{
    const char *why = 0;
    const struct row *type = 0;
    const struct row *hit = 0;
    uint16_t pin = 0, value = 0;

    if (!d->table_ok) {
        why = "table unknown, press 1";
    } else {
        for (uint8_t i = 0; i < d->nrows && !type; i++) {
            if (d->rows[i].letter == c[0]) {
                type = &d->rows[i];
            }
        }
        if (!type) {
            why = "unknown type";
        } else if (n != 1u + PIN_DIGITS + type->digits ||
                   parse_digits(c + 1, PIN_DIGITS, &pin) ||
                   parse_digits(c + 1 + PIN_DIGITS, type->digits, &value)) {
            why = "bad format";
        } else {
            for (uint8_t i = 0; i < d->nrows && !hit; i++) {
                if (d->rows[i].letter == c[0] && d->rows[i].pin == pin) {
                    hit = &d->rows[i];
                }
            }
            if (!hit) {
                why = "nothing of that type on that pin";
            } else if (value > hit->max) {
                why = "value too large";
            }
        }
    }

    if (why) {
        con_puts("rejected: ");
        con_putn(c, n);
        con_puts(" (");
        con_puts(why);
        con_puts(")\n");
        return -1;
    }
    return 0;
}

static void user_byte(struct dash *d, char c)
{
    if (d->mode == MENU) {
        if (c == '1') {
            con_puts("1\nasking AVR for its table...\n");
            send_query(d, 1);       /* table + menu print when "T END" arrives */
        } else if (c == '2') {
            con_putc('2');
            if (!d->table_ok) {
                con_puts("\ntable unknown, press 1 first\n");
                print_menu();
            } else {
                d->mode = CMD_ENTRY;
                d->cmd_len = 0;
                con_puts("\ncmd> ");
            }
        } else if (c == '\r' || c == '\n') {
            print_menu();
        }
        return;
    }

    /* CMD_ENTRY: collect a line, send on Enter. */
    if (c == '\r' || c == '\n') {
        con_putc('\n');
        if (d->cmd_len > 0 && validate(d, d->cmd, d->cmd_len) == 0) {
            con_puts("sent: ");
            con_putn(d->cmd, d->cmd_len);
            con_putc('\n');
            for (uint8_t i = 0; i < d->cmd_len; i++) {
                uart_tx(UART2_BASE, d->cmd[i]);
            }
            uart_tx(UART2_BASE, '\n');
            expect_reply(d);
        }
        d->mode = MENU;
        print_menu();
    } else if (c == 0x08 || c == 0x7F) {    /* Backspace / Delete */
        if (d->cmd_len > 0) {
            d->cmd_len--;
            con_puts("\b \b");
        }
    } else if (c >= 0x20 && c <= 0x7E && d->cmd_len < CMD_MAX) {
        d->cmd[d->cmd_len++] = c;
        con_putc(c);                /* echo */
    }
}

static void uart2_init(void)
{
    /* Clock on, pulse reset. UART_MEM is shared with UART0: enable, never reset. */
    REG(DPORT_PERIP_CLK_EN_REG) |= DPORT_UART2_BIT | DPORT_UART_MEM_BIT;
    REG(DPORT_PERIP_RST_EN_REG) |= DPORT_UART2_BIT;
    REG(DPORT_PERIP_RST_EN_REG) &= ~DPORT_UART2_BIT;

    REG(UART_CLKDIV(UART2_BASE)) = UART2_CLKDIV_VALUE;
    uint32_t conf0 = REG(UART_CONF0(UART2_BASE));
    conf0 &= ~(CONF0_PARITY_BITS | (3u << CONF0_BIT_NUM_SHIFT) | (3u << CONF0_STOP_SHIFT));
    conf0 |= (3u << CONF0_BIT_NUM_SHIFT) | (1u << CONF0_STOP_SHIFT) | CONF0_ERR_WR_MASK;
    REG(UART_CONF0(UART2_BASE)) = conf0;        /* 8N1, APB clock (default bit 27) */

    /* TX: GPIO17 pad -> GPIO matrix -> U2TXD, driven as output. */
    REG(IO_MUX_GPIO17_REG) = (REG(IO_MUX_GPIO17_REG) & ~MCU_SEL_MASK) | (PIN_FUNC_GPIO << MCU_SEL_SHIFT);
    REG(GPIO_FUNC_OUT_SEL_CFG(UART2_TX_GPIO)) = U2_SIGNAL_IDX | GPIO_OEN_SEL;
    REG(GPIO_ENABLE_W1TS_REG) = 1u << UART2_TX_GPIO;

    /* RX: GPIO16 pad (input enabled) -> GPIO matrix -> U2RXD. */
    REG(IO_MUX_GPIO16_REG) = (REG(IO_MUX_GPIO16_REG) & ~MCU_SEL_MASK) | (PIN_FUNC_GPIO << MCU_SEL_SHIFT) | FUN_IE;
    REG(GPIO_FUNC_IN_SEL_CFG(U2_SIGNAL_IDX)) = UART2_RX_GPIO | GPIO_SIG_IN_SEL;
}

void call_start_cpu0(void) __attribute__((section(".text.call_start_cpu0"), noreturn));

void call_start_cpu0(void)
{
    disable_wdt(RTC_WDTCONFIG0_REG, RTC_WDTWPROTECT_REG);
    disable_wdt(TIMG_WDTCONFIG0(TIMG0_BASE), TIMG_WDTWPROTECT(TIMG0_BASE));
    disable_wdt(TIMG_WDTCONFIG0(TIMG1_BASE), TIMG_WDTWPROTECT(TIMG1_BASE));

    uart2_init();

    struct dash d;                  /* no initializer: avoids an implicit memset (no libc) */
    d.nrows = 0;
    d.table_ok = 0;
    d.show_table = 0;
    d.avr_len = 0;
    d.avr_overflow = 0;
    d.cmd_len = 0;
    d.mode = MENU;
    d.waiting = 0;
    d.sent_at = 0;

    /* Sanity check of the 40 MHz assumption: ROM set UART0 to 115200, so its
     * integer divider should read 347 (40 MHz / 115200). */
    con_puts("\nESP32 dashboard boot. UART0 clkdiv=");
    con_num(REG(UART_CLKDIV(UART0_BASE)) & 0xFFFFFu, 0);
    con_puts(" (347 = 40 MHz APB, UART2 baud OK)\n");

    send_query(&d, 0);              /* learn the AVR's table silently */
    print_menu();

    for (;;) {
        int c = uart_rx(UART0_BASE, UART0_FIFO_AHB);
        if (c >= 0) {
            user_byte(&d, (char)c);
        }
        c = uart_rx(UART2_BASE, UART2_FIFO_AHB);
        if (c >= 0) {
            avr_byte(&d, (char)c);
        }
        if (d.waiting && (uint32_t)(ccount() - d.sent_at) >= REPLY_TIMEOUT_CYCLES) {
            d.waiting = 0;
            con_puts("\nAVR: no reply\n");
            if (d.show_table) {
                d.show_table = 0;
                print_menu();
            } else {
                redraw_prompt(&d);
            }
        }
    }
}
