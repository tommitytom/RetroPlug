// N8 standalone bridge - stage 1: MIDI IN.
//
// Reads MIDI on UART1 (GP5, physical pin 7) at 31250 baud through a 6N138
// optoisolator, decodes it with the reusable parser (midi.c) and prints one
// human-readable event per message to the console (UART0 / GP0-GP1, i.e. the
// Raspberry Pi Debug Probe serial port). The onboard LED toggles on every
// received byte as a no-terminal sanity check.
//
// Wiring (6N138, opto output side powered at 3.3V):
//   MIDI TRS Ring (DIN pin 4) --[220R]-- 6N138 pin 2 (LED anode)
//   MIDI TRS Tip  (DIN pin 5) ---------- 6N138 pin 3 (LED cathode)
//   1N4007 across pins 2<->3, band (cathode) to pin 2   (reverse-parallel)
//   MIDI TRS Sleeve ------------------- not connected (keep the isolation!)
//   6N138 pin 8 (Vcc) -- Pico 3V3 (pin 36)    [+ optional 0.1uF to GND]
//   6N138 pin 5 (GND) -- Pico GND (pin 38)
//   6N138 pin 6 (Vo)  -- 330R pull-up to 3V3, and to GP5 (UART1 RX, pin 7)
//   6N138 pin 7 (Vb)  -- 5k1 to GND           (sharpens the edge; don't omit)
//
// Clock (0xF8) and active-sensing (0xFE) are filtered from the log (see
// midi_print.c). Play a note and you get e.g. "NoteOn ch1 C5 (72) vel 69".

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "midi.h"
#include "midi_print.h"

#define MIDI_UART   uart1
#define MIDI_RX_PIN 5      // GP5 = UART1 RX = physical pin 7
#define MIDI_BAUD   31250

int main(void) {
    stdio_init_all();      // console -> UART0 (GP0/GP1) via the debug probe

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    uart_init(MIDI_UART, MIDI_BAUD);
    gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);

    midi_parser parser;
    midi_parser_init(&parser, midi_print, NULL);

    printf("\n[n8-midi] MIDI IN decoder on UART1/GP5 @ %d baud. "
           "Clock/sensing filtered - play a note...\n", MIDI_BAUD);

    while (true) {
        if (uart_is_readable(MIDI_UART)) {
            midi_parser_byte(&parser, uart_getc(MIDI_UART));
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);   // toggle on activity
#endif
        }
    }
}
