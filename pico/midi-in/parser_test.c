// Self-checking test for the MIDI parser - NOT part of the firmware build. host_test.c prints a
// captured stream for a human; this one asserts. Build + run:
//   gcc -std=c11 -Wall -I. -o /tmp/parser_test midi.c parser_test.c && /tmp/parser_test
// Exit 0 = every case passed.

#include <stdio.h>
#include <string.h>
#include "midi.h"

// What the parser emitted: channel-voice / system messages as "SS DD DD" and SysEx bytes as "sXX",
// in order, space-separated, so a case is one string compare.
static char out[1024];

static void put(const char *s) {
    if (out[0]) strncat(out, " ", sizeof out - strlen(out) - 1);
    strncat(out, s, sizeof out - strlen(out) - 1);
}

static void on_msg(const midi_message *m, void *user) {
    (void)user;
    char s[16];
    if (m->status >= 0xF8) snprintf(s, sizeof s, "%02X", m->status);
    else if (m->type == MIDI_PROGRAM_CHANGE || m->type == MIDI_CHANNEL_PRESSURE)
        snprintf(s, sizeof s, "%02X.%02X", m->status, m->data0);
    else snprintf(s, sizeof s, "%02X.%02X.%02X", m->status, m->data0, m->data1);
    put(s);
}

static void on_sysex(uint8_t b, void *user) {
    (void)user;
    char s[8];
    snprintf(s, sizeof s, "s%02X", b);
    put(s);
}

static int fails = 0;

static void check(const char *name, int with_sysex, const uint8_t *in, size_t n, const char *want) {
    midi_parser p;
    midi_parser_init(&p, on_msg, NULL);
    if (with_sysex) midi_parser_set_sysex_sink(&p, on_sysex);
    out[0] = 0;
    for (size_t i = 0; i < n; i++) midi_parser_byte(&p, in[i]);
    if (strcmp(out, want) == 0) { printf("ok   %s\n", name); return; }
    printf("FAIL %s\n     want: %s\n     got:  %s\n", name, want, out);
    fails++;
}

#define CASE(name, sx, want, ...) do { \
    const uint8_t in_[] = { __VA_ARGS__ }; check(name, sx, in_, sizeof in_, want); } while (0)

int main(void) {
    CASE("note on/off", 0, "90.45.7F 80.45.00", 0x90, 0x45, 0x7F, 0x80, 0x45, 0x00);
    CASE("running status", 0, "90.45.7F 90.48.7F 90.4C.00", 0x90, 0x45, 0x7F, 0x48, 0x7F, 0x4C, 0x00);
    CASE("realtime inside a message", 0, "F8 90.45.7F FE",
         0x90, 0xF8, 0x45, 0x7F, 0xFE);
    CASE("one-data-byte messages", 0, "C1.05 D2.40", 0xC1, 0x05, 0xD2, 0x40);
    CASE("SysEx skipped without a sink (unchanged)", 0, "90.45.7F",
         0xF0, 0x7D, 0x42, 0x02, 0xF7, 0x90, 0x45, 0x7F);

    CASE("SysEx passed through whole", 1, "sF0 s7D s42 s02 sF7",
         0xF0, 0x7D, 0x42, 0x02, 0xF7);
    CASE("realtime inside SysEx goes to the message sink", 1, "sF0 s7D F8 s42 sF7",
         0xF0, 0x7D, 0xF8, 0x42, 0xF7);
    CASE("status byte ends an open SysEx with a synthesised F7", 1, "sF0 s7D sF7 90.45.7F",
         0xF0, 0x7D, 0x90, 0x45, 0x7F);
    CASE("F0 inside a SysEx closes it and opens the next", 1, "sF0 s01 sF7 sF0 s02 sF7",
         0xF0, 0x01, 0xF0, 0x02, 0xF7);
    CASE("stray F7 is dropped", 1, "90.45.7F", 0xF7, 0x90, 0x45, 0x7F);
    CASE("SysEx cancels running status", 1, "90.45.7F sF0 s7D sF7",
         0x90, 0x45, 0x7F, 0xF0, 0x7D, 0xF7, 0x48, 0x7F);
    CASE("N163 wave upload framing", 1,
         "sF0 s7D s42 s01 s00 s08 s08 s08 sF7 90.45.7F",
         0xF0, 0x7D, 0x42, 0x01, 0x00, 0x08, 0x08, 0x08, 0xF7, 0x90, 0x45, 0x7F);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
