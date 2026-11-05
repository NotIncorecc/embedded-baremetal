/*
 * Power self-test for ATmega328P: checks whether the 5 V supply (taken from
 * the ESP32 board) can handle both SG90 servos and the LED.
 *
 * Runs ONCE per reset, printing each step on USART0 (9600 8N1, the same
 * link the ESP32 dashboard listens on) BEFORE executing it:
 *   "BOOT", LED flashes 3x quickly    <- start marker; seeing it again = a reset
 *   "TEST S09180" ...                  <- last line before an unexpected BOOT
 *                                         names the step that browned out
 *   "TEST DONE"                        <- pass
 *
 * Phases, lightest load first: single servo -> alternating -> both at once
 * -> LED ramp. Then both servos park at 90 deg and their pulses stop.
 * Hardware: servos on D9/D10 (Timer1), LED on D6 (Timer0), as in periph_uart.c.
 */
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#define UBRR_VALUE      103u        /* 9600 baud at 16 MHz */
#define PWM_TOP         39999u      /* Timer1: 0.5 us ticks, 20 ms period */
#define PULSE_MIN       2000u       /* 1.0 ms */
#define STEP_MS         1000u       /* time between steps */

struct step {
    const char *cmd;                /* same format as the UART protocol */
    uint8_t     then_wait;          /* 0 = run the next step immediately (simultaneous) */
};

static const struct step steps[] = {
    /* Phase 1: one servo at a time */
    { "S09000", 1 }, { "S09180", 1 }, { "S09090", 1 },
    { "S10000", 1 }, { "S10180", 1 }, { "S10090", 1 },
    /* Phase 2: alternating */
    { "S09000", 1 }, { "S10180", 1 }, { "S09180", 1 }, { "S10000", 1 },
    /* Phase 3: both at once (worst case), twice */
    { "S09000", 0 }, { "S10000", 1 }, { "S09180", 0 }, { "S10180", 1 },
    { "S09000", 0 }, { "S10000", 1 }, { "S09180", 0 }, { "S10180", 1 },
    /* Phase 4: LED ramp while the servos hold */
    { "L06000", 1 }, { "L06025", 1 }, { "L06050", 1 }, { "L06100", 1 }, { "L06000", 1 },
    /* Park */
    { "S09090", 0 }, { "S10090", 1 },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

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

static void wait_ms(uint16_t ms)
{
    while (ms--) {
        _delay_ms(1);               /* busy-wait; fine for a one-shot test */
    }
}

static void led_set(uint8_t percent)
{
    DDRD |= (1 << DDD6);
    if (percent == 0) {
        TCCR0A &= ~((1 << COM0A1) | (1 << COM0A0));    /* disconnect PWM, drive low */
        PORTD &= ~(1 << PD6);
        return;
    }
    OCR0A = (uint8_t)((percent * 255u) / 100u);
    TCCR0A |= (1 << COM0A1);
}

static void servo_set(uint8_t pin, uint8_t angle)
{
    uint16_t ticks = PULSE_MIN + (uint16_t)((angle * 100u) / 9u);
    if (pin == 9) {
        OCR1A = ticks;
        DDRB |= (1 << DDB1);
    } else {
        OCR1B = ticks;
        DDRB |= (1 << DDB2);
    }
}

/* Commands are hard-coded constants above, so no validation is needed here. */
static void run(const char *cmd)
{
    uint8_t  pin   = (uint8_t)((cmd[1] - '0') * 10 + (cmd[2] - '0'));
    uint16_t value = (uint16_t)((cmd[3] - '0') * 100 + (cmd[4] - '0') * 10 + (cmd[5] - '0'));
    if (cmd[0] == 'S') {
        servo_set(pin, (uint8_t)value);
    } else {
        led_set((uint8_t)value);
    }
}

int main(void)
{
    UBRR0H = 0;
    UBRR0L = UBRR_VALUE;
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);     /* 8N1 */
    UCSR0B = (1 << TXEN0);                      /* transmit only */

    ICR1   = PWM_TOP;                           /* Timer1 mode 14, clk/8 */
    TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
    TCCR0A = (1 << WGM01) | (1 << WGM00);       /* Timer0 fast PWM, clk/64 */
    TCCR0B = (1 << CS01) | (1 << CS00);

    uart_puts("BOOT\n");
    for (uint8_t i = 0; i < 3; i++) {           /* start marker */
        led_set(100);
        wait_ms(150);
        led_set(0);
        wait_ms(150);
    }

    for (uint8_t i = 0; i < COUNT(steps); i++) {
        uart_puts("TEST ");
        uart_puts(steps[i].cmd);
        uart_putc('\n');
        run(steps[i].cmd);
        if (steps[i].then_wait) {
            wait_ms(STEP_MS);
        }
    }

    DDRB &= ~((1 << DDB1) | (1 << DDB2));       /* stop servo pulses: servos relax */
    uart_puts("TEST DONE\n");

    while (1) { }
}
