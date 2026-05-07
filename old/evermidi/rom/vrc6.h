#ifndef VRC6_H
#define VRC6_H

#include "main.h"

/* ===== VRC6 register definitions ===== */

/* VRC6 Pulse 1 ($9000-$9002) */
#define VRC6_P1_CTRL   (*(volatile u8 *)0x9000)  /* mode, duty[2:0], volume[3:0] */
#define VRC6_P1_LO     (*(volatile u8 *)0x9001)  /* timer low 8 bits */
#define VRC6_P1_HI     (*(volatile u8 *)0x9002)  /* enable, timer high [3:0] */

/* VRC6 Pulse 2 ($A000-$A002) */
#define VRC6_P2_CTRL   (*(volatile u8 *)0xA000)
#define VRC6_P2_LO     (*(volatile u8 *)0xA001)
#define VRC6_P2_HI     (*(volatile u8 *)0xA002)

/* VRC6 Sawtooth ($B000-$B002) */
#define VRC6_SAW_RATE  (*(volatile u8 *)0xB000)  /* accumulator rate [5:0] */
#define VRC6_SAW_LO    (*(volatile u8 *)0xB001)  /* timer low 8 bits */
#define VRC6_SAW_HI    (*(volatile u8 *)0xB002)  /* enable, timer high [3:0] */

/* VRC6 frequency control ($9003) */
#define VRC6_FREQ_CTRL (*(volatile u8 *)0x9003)  /* halt, 4x, 256x scaling */

/* ===== MIDI channel assignments for VRC6 ===== */
#define MIDI_CH_VRC6_P1    0x05
#define MIDI_CH_VRC6_P2    0x06
#define MIDI_CH_VRC6_SAW   0x07

/* Max sawtooth accumulator rate before distortion.
 * The saw accumulates rate*2 per step over 7 steps then resets.
 * 42 * 6 = 252, just under the 8-bit overflow. */
#define SAW_MAX_RATE 42

void vrc6_init(void);
void vrc6_p1_note_on(u8 note, u8 vel);
void vrc6_p1_note_off(void);
void vrc6_p2_note_on(u8 note, u8 vel);
void vrc6_p2_note_off(void);
void vrc6_saw_note_on(u8 note, u8 vel);
void vrc6_saw_note_off(void);
void handle_cc_vrc6_pulse(u8 ch, u8 cc, u8 val);
void handle_cc_vrc6_saw(u8 cc, u8 val);

#endif /* VRC6_H */
