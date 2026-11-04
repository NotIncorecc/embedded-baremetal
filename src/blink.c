/*
 * Bare-metal blink for ATmega328P (Uno-style board, on-board LED on PB5).
 *
 * Timer1 runs in CTC mode and fires a compare-match interrupt every 2 s.
 * The interrupt flips the LED, giving 2 s on / 2 s off. main() only sets
 * things up; the CPU is never stuck in a delay loop.
 */
#include <avr/io.h>
#include <avr/interrupt.h>

/* 16 MHz / 1024 prescaler = 15625 ticks per second; 2 s = 31250 ticks. */
#define BLINK_TICKS 31250u

/* Runs each time TCNT1 reaches OCR1A (and is reset to 0 by CTC mode). */
ISR(TIMER1_COMPA_vect) /* Timer1 being used here*/
{
    PORTB ^= (1 << PB5);            /* XOR flips only bit 5: on -> off -> on */
}

int main(void)
{
    DDRB |= (1 << DDB5);            /* PB5 as output, other port B pins untouched */

    TCCR1A = 0;                     /* no PWM / output-pin features */
    TCNT1  = 0;                     /* start counting from 0 */
    OCR1A  = BLINK_TICKS - 1;       /* counter goes 0..31249 = 31250 ticks */
    TIMSK1 = (1 << OCIE1A);         /* enable compare-match A interrupt */

    /* Written last because setting the CS bits starts the timer:
    The WGM mode number is 4 bits split across both registers: WGM13 and WGM12 live in TCCR1B, and WGM11 and WGM10 in TCCR1A. 
    We get 0100 = mode 4, CTC with the top value in OCR1A.
    , CS12|CS10 = clock / 1024. */
    TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);

    sei();                          /* global interrupt enable */

    while (1) {
        /* Nothing to do: all work happens in the interrupt. */
    }
}
