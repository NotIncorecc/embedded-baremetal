/*
 * Bare-metal peripheral controller for ATmega328P (Uno-style board).
 *
 * Receives commands on USART0 (pins D0/D1, 9600 8N1) and drives the
 * peripherals declared in the pin table below.
 *
 * Command = type letter + 2-digit pin + value digits + '\n'
 *   S<pin><angle>    servo, angle 000..180          e.g. "S09090\n"
 *   L<pin><percent>  LED brightness, 000..100 %     e.g. "L06050\n"
 *
 * Replies:
 *   "OK <command>\n"  output updated (open loop: a servo cannot report that it moved)
 *   "ERR INVALID\n"   bad format, value out of range, or type/pin not in the table
 *   "ERR\n"           UART hardware error (framing/overrun) on that line
 *   "BOOT\n"          sent once at reset (spot brownouts in the log)
 * On any error, outputs are left unchanged.
 *
 * Table query: "?\n" -> one line per declared output, then an end marker:
 *   "T S09 180\n" ... "T END\n"   (letter+pin, then max value; the width of
 *                                   the max field = number of value digits)
 * The AVR is the single source of truth; the ESP32 asks and validates against it.
 *
 * The AVR cannot detect what is wired to a pin; the pin table is the
 * designer's declaration. To add a peripheral: add a row to cmd_types[]
 * (if it is a new type) and a row to outputs[].
 */
#include <avr/io.h>
#include <stdint.h>

#define UBRR_VALUE      103u        /* 16 MHz / (16 * 9600) - 1 = 103.17 -> 0.2 % error */

/* --- Servo PWM: Timer1 mode 14 (Fast PWM, TOP = ICR1), prescaler 8 ---
 * 16 MHz / 8 = 2 MHz -> 1 tick = 0.5 us.  20 ms period = 40000 ticks.
 * OC1A (D9) and OC1B (D10) share this period, each with its own compare value. */
#define PWM_TOP         39999u
#define PULSE_MIN       2000u       /* 1.0 ms -> angle 0   (SG90 can go ~0.5 ms; widen carefully) */
#define PULSE_MAX       4000u       /* 2.0 ms -> angle 180 (SG90 can go ~2.4 ms; widen carefully) */

#define PIN_DIGITS      2u          /* every command: letter + 2-digit pin + value */
#define MAX_LINE        6u          /* longest command, '\n' not counted */

/* ---------- Output drivers: one function per physical pin ---------- */

static uint16_t angle_to_ticks(uint16_t angle)
{
    return PULSE_MIN + (uint16_t)((angle * 100u) / 9u);    /* 2000/180 = 100/9 */
}

static void servo_d9(uint16_t angle)
{
    OCR1A = angle_to_ticks(angle);  /* double-buffered: applied at next period */
    DDRB |= (1 << DDB1);            /* pulses reach the pin from the first command on */
}

static void servo_d10(uint16_t angle)
{
    OCR1B = angle_to_ticks(angle);
    DDRB |= (1 << DDB2);
}

static void led_d6(uint16_t percent)
{
    DDRD |= (1 << DDD6);
    if (percent == 0) {
        /* Fast PWM with OCR0A = 0 still emits a 1-tick spike each period,
         * so disconnect the timer and drive the pin low instead. */
        TCCR0A &= ~((1 << COM0A1) | (1 << COM0A0));
        PORTD &= ~(1 << PD6);
        return;
    }
    OCR0A = (uint8_t)((percent * 255u) / 100u);  /* 100 % -> 255 = always high */
    TCCR0A |= (1 << COM0A1);                     /* non-inverting PWM on OC0A */
}

/* ---------- Tables ---------- */

struct cmd_type {
    char     letter;
    uint8_t  value_digits;
    uint16_t max_value;
};

static const struct cmd_type cmd_types[] = {
    { 'S', 3, 180 },                /* servo angle in degrees */
    { 'L', 3, 100 },                /* LED brightness in percent */
};

struct output {
    char    letter;
    uint8_t pin;                    /* Arduino D-number, as written in the command */
    void  (*set)(uint16_t value);
};

static const struct output outputs[] = {
    { 'S',  9, servo_d9  },
    { 'S', 10, servo_d10 },
    { 'L',  6, led_d6    },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ---------- UART ---------- */

static void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)UBRR_VALUE;
    UCSR0A = 0;                                 /* U2X0 off */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);     /* 8N1 */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) { }
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

/* ---------- Timers ---------- */

static void timers_init(void)
{
    /* Timer1: servos. Mode 14 = WGM13:10 = 1110; COM1x1 = non-inverting on A and B.
     * TCCR1B written last: CS11 (clk/8) starts the timer. */
    ICR1   = PWM_TOP;
    OCR1A  = angle_to_ticks(90);
    OCR1B  = angle_to_ticks(90);
    TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);

    /* Timer0: LED. Fast PWM mode 3 (TOP = 0xFF), clk/64 -> 16 MHz/64/256 = ~977 Hz.
     * OC0A stays disconnected until led_d6() turns it on. */
    TCCR0A = (1 << WGM01) | (1 << WGM00);
    TCCR0B = (1 << CS01) | (1 << CS00);
}

/* ---------- Parsing ---------- */

/* Reads n ASCII digits. Returns 0 and fills *out, or -1 if any is not a digit. */
static int8_t read_digits(const char *s, uint8_t n, uint16_t *out)
{
    uint16_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10u + (uint16_t)(s[i] - '0');
    }
    *out = v;
    return 0;
}

static const struct cmd_type *find_type(char letter)
{
    for (uint8_t i = 0; i < COUNT(cmd_types); i++) {
        if (cmd_types[i].letter == letter) {
            return &cmd_types[i];
        }
    }
    return 0;
}

/* Prints v as exactly `digits` decimal digits, zero-padded. */
static void put_num(uint16_t v, uint8_t digits)
{
    char d[5];
    for (uint8_t i = digits; i > 0; i--) {
        d[i - 1] = (char)('0' + v % 10u);
        v /= 10u;
    }
    for (uint8_t i = 0; i < digits; i++) {
        uart_putc(d[i]);
    }
}

/* Answer to "?": the outputs table, one row per line, then "T END". */
static void send_table(void)
{
    for (uint8_t i = 0; i < COUNT(outputs); i++) {
        const struct cmd_type *type = find_type(outputs[i].letter);
        uart_puts("T ");
        uart_putc(outputs[i].letter);
        put_num(outputs[i].pin, PIN_DIGITS);
        uart_putc(' ');
        put_num(type->max_value, type->value_digits);
        uart_putc('\n');
    }
    uart_puts("T END\n");
}

/* Validates one line and applies it. Returns 0 on success, -1 if invalid. */
static int8_t execute(const char *buf, uint8_t len)
{
    const struct cmd_type *type = (len > 0) ? find_type(buf[0]) : 0;
    if (!type || len != 1u + PIN_DIGITS + type->value_digits) {
        return -1;
    }

    uint16_t pin, value;
    if (read_digits(buf + 1, PIN_DIGITS, &pin) ||
        read_digits(buf + 1 + PIN_DIGITS, type->value_digits, &value) ||
        value > type->max_value) {
        return -1;
    }

    for (uint8_t i = 0; i < COUNT(outputs); i++) {
        if (outputs[i].letter == type->letter && outputs[i].pin == pin) {
            outputs[i].set(value);
            return 0;
        }
    }
    return -1;                      /* type is known, but nothing declared on that pin */
}

static void handle_line(const char *buf, uint8_t len)
{
    if (len == 1 && buf[0] == '?') {
        send_table();
        return;
    }
    if (execute(buf, len) < 0) {
        uart_puts("ERR INVALID\n");
        return;
    }
    uart_puts("OK ");
    for (uint8_t i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
    uart_putc('\n');
}

/* ---------- Main loop: two-state line receiver ---------- */

enum rx_state {
    COLLECTING,                     /* storing bytes until '\n' */
    DISCARDING                      /* line already bad: skip to next '\n' */
};

int main(void)
{
    uart_init();
    timers_init();
    uart_puts("BOOT\n");

    enum rx_state state = COLLECTING;
    uint8_t hw_error = 0;
    char buf[MAX_LINE];
    uint8_t len = 0;

    while (1) {
        if (!(UCSR0A & (1 << RXC0))) {
            continue;               /* PWM runs in hardware meanwhile */
        }
        uint8_t status = UCSR0A;    /* read BEFORE UDR0: FE0/DOR0 describe this byte */
        char c = (char)UDR0;

        if (status & ((1 << FE0) | (1 << DOR0))) {
            hw_error = 1;
            state = DISCARDING;
        }

        if (c == '\n') {
            if (hw_error) {
                uart_puts("ERR\n");
            } else if (state == DISCARDING) {
                uart_puts("ERR INVALID\n");     /* line longer than any command */
            } else {
                handle_line(buf, len);
            }
            state = COLLECTING;
            hw_error = 0;
            len = 0;
            continue;
        }

        if (state == COLLECTING) {
            if (len < MAX_LINE) {
                buf[len++] = c;
            } else {
                state = DISCARDING;
            }
        }
    }
}
