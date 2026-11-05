/*
 * Bare-metal servo controller for ATmega328P (Uno-style board).
 *
 * Receives angle commands on USART0 (pins D0/D1, 9600 8N1) and drives an
 * SG90 servo on D9 (PB1/OC1A) with Timer1 hardware PWM.
 *
 * Protocol (one command per line):
 *   command : exactly 3 ASCII digits + '\n', value 000..180   e.g. "090\n"
 *   replies : "OK ddd\n"       pulse updated (servo has no feedback, so this
 *                              does NOT prove it moved)
 *             "ERR INVALID\n"  not 3 digits, or > 180   -> servo unchanged
 *             "ERR\n"          UART hardware error (framing/overrun) -> unchanged
 *             "BOOT\n"         sent once at reset (spot brownouts in the log)
 *
 * No servo pulses are produced until the first valid command.
 */
#include <avr/io.h>
#include <stdint.h>

/* --- UART --- */
#define UBRR_VALUE      103u        /* 16 MHz / (16 * 9600) - 1 = 103.17 -> 0.2 % error */

/* --- Servo PWM: Timer1 mode 14 (Fast PWM, TOP = ICR1), prescaler 8 ---
 * 16 MHz / 8 = 2 MHz -> 1 tick = 0.5 us.  20 ms period = 40000 ticks. */
#define PWM_TOP         39999u      /* counter runs 0..39999 = 40000 ticks */
#define PULSE_MIN       2000u       /* 1.0 ms -> angle 0   (widen carefully, SG90 ~0.5 ms) */
#define PULSE_MAX       4000u       /* 2.0 ms -> angle 180 (widen carefully, SG90 ~2.4 ms) */
#define ANGLE_MAX       180u

#define CMD_LEN         3u          /* digits per command, '\n' not counted */

/* Receiver state machine. */
enum rx_state {
    COLLECTING,                     /* storing bytes until '\n' */
    DISCARDING                      /* line already bad: skip to next '\n' */
};

static void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)UBRR_VALUE;
    UCSR0A = 0;                                 /* U2X0 off: normal speed */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);     /* 8 data bits, no parity, 1 stop */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);       /* enable receiver and transmitter */
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) { }        /* wait until transmit buffer is empty */
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static void pwm_init(void)
{
    ICR1  = PWM_TOP;                            /* sets the 20 ms period */
    OCR1A = PULSE_MIN + (PULSE_MAX - PULSE_MIN) / 2u;   /* 90 deg; only matters once D9 is enabled */
    /* COM1A1: clear OC1A on compare match, set at BOTTOM (non-inverting PWM).
     * Mode 14 = WGM13:WGM10 = 1110, split across both registers.
     * TCCR1B written last: setting CS11 (clk/8) starts the timer. */
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
}

static void servo_set(uint8_t angle)
{
    /* Linear map 0..180 -> PULSE_MIN..PULSE_MAX. 2000/180 = 100/9; 180*100 fits in 16 bits. */
    uint16_t ticks = PULSE_MIN + (uint16_t)((angle * 100u) / 9u);
    OCR1A = ticks;              /* double-buffered: takes effect at the next period, no glitch */
    DDRB |= (1 << DDB1);        /* D9 as output: pulses reach the servo from now on */
}

/* Returns 0..180, or -1 if buf is not a valid command. */
static int16_t parse_angle(const char *buf, uint8_t len)
{
    if (len != CMD_LEN) {
        return -1;
    }
    uint16_t value = 0;
    for (uint8_t i = 0; i < CMD_LEN; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint16_t)(buf[i] - '0');
    }
    return (value <= ANGLE_MAX) ? (int16_t)value : -1;
}

static void handle_line(const char *buf, uint8_t len)
{
    int16_t angle = parse_angle(buf, len);
    if (angle < 0) {
        uart_puts("ERR INVALID\n");
        return;
    }
    servo_set((uint8_t)angle);
    uart_puts("OK ");
    for (uint8_t i = 0; i < CMD_LEN; i++) {
        uart_putc(buf[i]);
    }
    uart_putc('\n');
}

int main(void)
{
    uart_init();
    pwm_init();
    uart_puts("BOOT\n");

    enum rx_state state = COLLECTING;
    uint8_t hw_error = 0;           /* current line hit a framing/overrun error */
    char buf[CMD_LEN];
    uint8_t len = 0;

    while (1) {
        if (!(UCSR0A & (1 << RXC0))) {
            continue;               /* nothing received yet; PWM keeps running in hardware */
        }
        /* Status must be read BEFORE UDR0: FE0/DOR0 describe the byte in the buffer. */
        uint8_t status = UCSR0A;
        char c = (char)UDR0;

        if (status & ((1 << FE0) | (1 << DOR0))) {
            hw_error = 1;
            state = DISCARDING;
        }

        if (c == '\n') {
            if (hw_error) {
                uart_puts("ERR\n");
            } else if (state == DISCARDING) {
                uart_puts("ERR INVALID\n");     /* line was too long */
            } else {
                handle_line(buf, len);
            }
            state = COLLECTING;     /* every '\n' starts a fresh line */
            hw_error = 0;
            len = 0;
            continue;
        }

        if (state == COLLECTING) {
            if (len < CMD_LEN) {
                buf[len++] = c;
            } else {
                state = DISCARDING; /* 4th character before '\n': too long */
            }
        }
        /* DISCARDING: ignore everything until '\n'. */
    }
}
