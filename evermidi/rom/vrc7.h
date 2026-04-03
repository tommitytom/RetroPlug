#ifndef VRC7_H
#define VRC7_H

#include "main.h"

/* ===== VRC7 (YM2413) register definitions ===== */

/* VRC7 address/data ports */
#define VRC7_ADDR      (*(volatile u8 *)0x9010)
#define VRC7_DATA      (*(volatile u8 *)0x9030)

/* VRC7 has 6 FM channels, each with a carrier+modulator pair.
 * Custom instrument patch is registers $00-$07.
 * Per-channel registers:
 *   $10-$15 : F-Number low 8 bits
 *   $20-$25 : sustain, key-on, octave[3:1], F-Number high bit
 *   $30-$35 : instrument[7:4], volume[3:0]
 */

/* ===== MIDI channel assignments for VRC7 ===== */
#define MIDI_CH_VRC7_0   0x05
#define MIDI_CH_VRC7_1   0x06
#define MIDI_CH_VRC7_2   0x07
#define MIDI_CH_VRC7_3   0x08
#define MIDI_CH_VRC7_4   0x09
#define MIDI_CH_VRC7_5   0x0A

/* Number of VRC7 channels */
#define VRC7_CH_COUNT 6

void vrc7_init(void);
void vrc7_note_on(u8 ch, u8 note, u8 vel);
void vrc7_note_off(u8 ch);
void handle_cc_vrc7(u8 ch, u8 cc, u8 val);

#endif /* VRC7_H */
