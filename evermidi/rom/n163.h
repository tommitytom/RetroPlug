#ifndef N163_H
#define N163_H

#include "main.h"

/* ===== Namco 163 register definitions ===== */

/* N163 uses address auto-increment via port $F800, data at $4800 */
#define N163_ADDR      (*(volatile u8 *)0xF800)
#define N163_DATA      (*(volatile u8 *)0x4800)

/*
 * N163 internal register map (per channel, 8 bytes each):
 * Channels are numbered 0-7, but mapped in reverse in the register file:
 *   Channel 0 = highest numbered channel at $78-$7F
 *   Channel N uses base = $78 - (N * 8)  ... when max_ch = N
 *
 * Actually the convention is:
 *   Registers $40-$47 = channel 0 (when using fewer channels)
 *   ...
 *   Registers $78-$7F = channel 7 (always the last)
 *
 * Per channel (base + offset):
 *   +0 : Frequency low 8 bits
 *   +1 : Phase low 8 bits
 *   +2 : Frequency mid 8 bits
 *   +3 : Phase mid 8 bits
 *   +4 : Frequency high 2 bits [1:0], wave length [4:2] (encoded as 256 - (len*4))
 *   +5 : Phase high 8 bits
 *   +6 : Wave address (start position in wave RAM)
 *   +7 : Volume [3:0], (channel 7 only: num_channels [6:4] in reg $7F)
 */

/* We use 4 channels (configurable).
 * With 4 active channels, the update rate is: 1789773 / (15 * 4) = ~29829 Hz
 * usable channels start at register base $60 for ch0 through $78 for ch3. */
#define N163_NUM_CHANNELS 4

/* MIDI channel assignments for N163 */
#define MIDI_CH_N163_0   0x05
#define MIDI_CH_N163_1   0x06
#define MIDI_CH_N163_2   0x07
#define MIDI_CH_N163_3   0x08

void n163_init(void);
void n163_note_on(u8 ch, u8 note, u8 vel);
void n163_note_off(u8 ch);
void handle_cc_n163(u8 ch, u8 cc, u8 val);

#endif /* N163_H */
