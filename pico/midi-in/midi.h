// A small, host-agnostic MIDI byte-stream parser. Pure C, no Pico/hardware deps,
// so it is reused by the firmware, by later stages (the N8 bridge forwards these
// events), and by an offline host test. Feed it raw bytes; it calls a sink with a
// decoded message per complete MIDI message.
//
// Handles: channel-voice messages (with running status), system realtime bytes
// (which may interleave anywhere and do NOT disturb an in-progress message),
// system-common messages, and SysEx (payload skipped).

#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

// High-nibble type for channel-voice messages; full status byte for system msgs.
typedef enum {
    MIDI_NOTE_OFF         = 0x80,
    MIDI_NOTE_ON          = 0x90,
    MIDI_POLY_AFTERTOUCH  = 0xA0,
    MIDI_CONTROL_CHANGE   = 0xB0,
    MIDI_PROGRAM_CHANGE   = 0xC0,
    MIDI_CHANNEL_PRESSURE = 0xD0,
    MIDI_PITCH_BEND       = 0xE0,
} midi_type;

typedef struct {
    uint8_t status;   // full status byte (voice: type|channel; system: the status)
    uint8_t type;     // voice: status & 0xF0; system: == status
    uint8_t channel;  // 0..15 (voice messages only)
    uint8_t data0;    // first data byte (meaning depends on type)
    uint8_t data1;    // second data byte (0 if the message carries only one)
} midi_message;

typedef void (*midi_sink)(const midi_message *msg, void *user);

typedef struct {
    midi_sink sink;
    void    *user;
    uint8_t  running_status; // last channel-voice status, for running status
    uint8_t  cur_status;     // status whose data bytes are being collected
    uint8_t  data[2];
    uint8_t  have;           // data bytes collected so far
    uint8_t  need;           // data bytes required for cur_status
    uint8_t  in_sysex;       // inside an F0..F7 SysEx (payload ignored)
} midi_parser;

void midi_parser_init(midi_parser *p, midi_sink sink, void *user);
void midi_parser_byte(midi_parser *p, uint8_t b);

#endif // MIDI_H
