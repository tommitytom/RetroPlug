// Offline exerciser for the MIDI parser - NOT part of the firmware build.
// Feeds a file of raw MIDI bytes through midi.c -> midi_print.c, so the parser
// can be validated on captured hardware streams without a Pico. Build:
//   gcc -std=c11 -I. -o /tmp/miditest midi.c midi_print.c host_test.c
//   /tmp/miditest <raw-midi-bytes-file>

#include <stdio.h>
#include "midi.h"
#include "midi_print.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <raw-midi-file>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }

    midi_parser p;
    midi_parser_init(&p, midi_print, NULL);

    int c;
    while ((c = fgetc(f)) != EOF) midi_parser_byte(&p, (uint8_t)c);

    fclose(f);
    return 0;
}
