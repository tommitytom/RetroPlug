#include <stdio.h>
#include "midi_print.h"

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// MIDI note -> scientific pitch name (note 60 = C4 = middle C).
static void note_name(uint8_t n, char *buf, int sz) {
    snprintf(buf, sz, "%s%d", NOTE_NAMES[n % 12], (int)(n / 12) - 1);
}

void midi_print(const midi_message *m, void *user) {
    (void)user;
    char nb[8];
    int ch = m->channel + 1; // 1-based for humans

    switch (m->type) {
        case MIDI_NOTE_ON:
            note_name(m->data0, nb, sizeof nb);
            if (m->data1 == 0) // note-on velocity 0 == note-off
                printf("NoteOff ch%-2d %-4s (%3d)\n", ch, nb, m->data0);
            else
                printf("NoteOn  ch%-2d %-4s (%3d) vel %d\n", ch, nb, m->data0, m->data1);
            break;
        case MIDI_NOTE_OFF:
            note_name(m->data0, nb, sizeof nb);
            printf("NoteOff ch%-2d %-4s (%3d)\n", ch, nb, m->data0);
            break;
        case MIDI_POLY_AFTERTOUCH:
            note_name(m->data0, nb, sizeof nb);
            printf("PolyAT  ch%-2d %-4s (%3d) = %d\n", ch, nb, m->data0, m->data1);
            break;
        case MIDI_CONTROL_CHANGE:
            printf("CC      ch%-2d #%-3d = %d\n", ch, m->data0, m->data1);
            break;
        case MIDI_PROGRAM_CHANGE:
            printf("Program ch%-2d = %d\n", ch, m->data0);
            break;
        case MIDI_CHANNEL_PRESSURE:
            printf("ChanAT  ch%-2d = %d\n", ch, m->data0);
            break;
        case MIDI_PITCH_BEND: {
            int bend = ((int)m->data1 << 7 | m->data0) - 8192; // 14-bit, centered
            printf("Bend    ch%-2d = %+d\n", ch, bend);
            break;
        }
        default: // system messages (m->status is the full status byte)
            switch (m->status) {
                case 0xFA: printf("-- Start --\n"); break;
                case 0xFB: printf("-- Continue --\n"); break;
                case 0xFC: printf("-- Stop --\n"); break;
                case 0xFF: printf("-- Reset --\n"); break;
                // 0xF8 clock + 0xFE active-sensing intentionally filtered.
                default: break;
            }
            break;
    }
}
