#include "midi.h"

// Data bytes carried by a channel-voice status byte.
static uint8_t voice_data_len(uint8_t status) {
    switch (status & 0xF0) {
        case MIDI_NOTE_OFF:
        case MIDI_NOTE_ON:
        case MIDI_POLY_AFTERTOUCH:
        case MIDI_CONTROL_CHANGE:
        case MIDI_PITCH_BEND:
            return 2;
        case MIDI_PROGRAM_CHANGE:
        case MIDI_CHANNEL_PRESSURE:
            return 1;
        default:
            return 0;
    }
}

void midi_parser_init(midi_parser *p, midi_sink sink, void *user) {
    p->sink = sink;
    p->user = user;
    p->running_status = 0;
    p->cur_status = 0;
    p->have = 0;
    p->need = 0;
    p->in_sysex = 0;
}

// Emit the message currently assembled under `status` (data in p->data).
static void emit(midi_parser *p, uint8_t status) {
    midi_message m;
    m.status = status;
    if (status >= 0xF0) {
        m.type = status;
        m.channel = 0;
    } else {
        m.type = status & 0xF0;
        m.channel = status & 0x0F;
    }
    m.data0 = p->data[0];
    m.data1 = p->data[1];
    if (p->sink) p->sink(&m, p->user);
}

void midi_parser_byte(midi_parser *p, uint8_t b) {
    // System realtime (0xF8..0xFF): single byte, may interleave anywhere, and
    // must not disturb a message currently being assembled or running status.
    if (b >= 0xF8) {
        midi_message m = { .status = b, .type = b, .channel = 0, .data0 = 0, .data1 = 0 };
        if (p->sink) p->sink(&m, p->user);
        return;
    }

    // Status byte (0x80..0xF7).
    if (b >= 0x80) {
        if (b == 0xF0) { p->in_sysex = 1; p->running_status = 0; p->cur_status = 0; return; }
        if (b == 0xF7) { p->in_sysex = 0; p->running_status = 0; p->cur_status = 0; return; }
        p->in_sysex = 0;

        if (b >= 0xF1 && b <= 0xF6) {
            // System common cancels running status.
            p->running_status = 0;
            p->cur_status = b;
            p->have = 0;
            p->need = (b == 0xF2) ? 2 : (b == 0xF1 || b == 0xF3) ? 1 : 0;
            if (p->need == 0) { emit(p, b); p->cur_status = 0; }
            return;
        }

        // Channel-voice status.
        p->running_status = b;
        p->cur_status = b;
        p->have = 0;
        p->need = voice_data_len(b);
        return;
    }

    // Data byte (0x00..0x7F).
    if (p->in_sysex) return; // ignore SysEx payload

    if (p->cur_status == 0) {
        if (p->running_status == 0) return; // stray data byte, no status yet
        p->cur_status = p->running_status;  // running status: reuse last voice status
        p->have = 0;
        p->need = voice_data_len(p->running_status);
    }

    p->data[p->have++] = b;
    if (p->have >= p->need) {
        emit(p, p->cur_status);
        p->have = 0;
        // Keep running status for voice messages; system common has none.
        p->cur_status = (p->cur_status >= 0xF0) ? 0 : p->running_status;
    }
}
