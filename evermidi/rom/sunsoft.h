#ifndef SUNSOFT_H
#define SUNSOFT_H

#include "main.h"

/* ===== Sunsoft 5B (FME-7 / YM2149F) register definitions ===== */

/* Sunsoft 5B uses command/data ports like the AY-3-8910 */
#define S5B_ADDR       (*(volatile u8 *)0xC000)
#define S5B_DATA       (*(volatile u8 *)0xE000)

/* AY register numbers */
#define S5B_REG_A_LO     0x00  /* Channel A tone period low 8 bits */
#define S5B_REG_A_HI     0x01  /* Channel A tone period high 4 bits */
#define S5B_REG_B_LO     0x02  /* Channel B tone period low 8 bits */
#define S5B_REG_B_HI     0x03  /* Channel B tone period high 4 bits */
#define S5B_REG_C_LO     0x04  /* Channel C tone period low 8 bits */
#define S5B_REG_C_HI     0x05  /* Channel C tone period high 4 bits */
#define S5B_REG_NOISE    0x06  /* Noise period (5 bits) */
#define S5B_REG_MIXER    0x07  /* Tone/noise enable (active low) */
#define S5B_REG_A_VOL    0x08  /* Channel A volume (4 bits + envelope mode) */
#define S5B_REG_B_VOL    0x09  /* Channel B volume */
#define S5B_REG_C_VOL    0x0A  /* Channel C volume */
#define S5B_REG_ENV_LO   0x0B  /* Envelope period low */
#define S5B_REG_ENV_HI   0x0C  /* Envelope period high */
#define S5B_REG_ENV_SHAPE 0x0D /* Envelope shape */

/* ===== MIDI channel assignments for Sunsoft 5B ===== */
#define MIDI_CH_S5B_A    0x05
#define MIDI_CH_S5B_B    0x06
#define MIDI_CH_S5B_C    0x07

#define S5B_CH_COUNT 3

void s5b_init(void);
void s5b_note_on(u8 ch, u8 note, u8 vel);
void s5b_note_off(u8 ch);
void handle_cc_s5b(u8 ch, u8 cc, u8 val);

#endif /* SUNSOFT_H */
