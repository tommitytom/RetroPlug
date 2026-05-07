/*
 * File:   main.h
 * Author: Igor
 *
 * Created on December 14, 2024, 9:13 PM
 */

#ifndef MAIN_H
#define	MAIN_H

#define u8 unsigned char
#define u16 unsigned short
#define u32 unsigned long

#include "sys.h"
#include "everdrive.h"
#include "errors.h"

/* ===== MIDI CC numbers =====
 *
 *   CC 1  (Mod Wheel)       -> Duty cycle (APU pulse: 4 levels, VRC6 pulse: 8 levels)
 *   CC 7  (Channel Volume)  -> Volume (all except triangle)
 *   CC 75 (Sound Ctrl 6)    -> Sweep direction (APU pulse only): 0-42=off, 43-85=down, 86-127=up
 *   CC 76 (Sound Ctrl 7)    -> Sweep shift (APU pulse only): 0=off, 1-127 maps to shift 1-7
 *   CC 123 (All Notes Off)  -> Silence the channel
 */
#define CC_MOD_WHEEL     1
#define CC_VOLUME        7
#define CC_SWEEP_DIR     75
#define CC_SWEEP_SHIFT   76
#define CC_ALL_NOTES_OFF 123

/* DMC-specific CC numbers (MIDI channel 5) */
#define CC_DMC_PCM_VAL     2
#define CC_DMC_RATE        3
#define CC_DMC_LOOP        4
#define CC_DMC_ADDR_OFS    5
#define CC_DMC_LEN_OVR     6
#define CC_DMC_ADDR_EN     8
#define CC_DMC_PCM_EN     13
#define CC_DMC_BANK       14

/* VRC7 custom patch register CCs: CC 102-109 → VRC7 regs $00-$07 */
#define CC_PATCH_REG0    102
#define CC_PATCH_REG7    109

/* ===== PAL/NTSC region =====
 *
 * g_is_pal is set at startup by timing VBlank intervals.
 * NTSC CPU: 1,789,773 Hz  PAL CPU: 1,662,607 Hz
 *
 * Timer channels (APU, VRC6, S5B):
 *   t_pal = (t_ntsc + 1) * 1662 / 1789 - 1  (timer shrinks on PAL)
 *
 * Frequency-register channels (N163, VRC7):
 *   f_pal = f_ntsc * 1789 / 1662             (freq reg grows on PAL)
 *
 * The 1662/1789 fraction approximates 1662607/1789773 to within 0.002%.
 */
extern u8 g_is_pal;

#define PAL_SCALE_TIMER(t) \
    ((u16)(((u32)((t) + 1) * 1662u + 894u) / 1789u) - 1)

#define PAL_SCALE_FREQ(f) \
    ((u32)((u32)(f) * 1789u + 831u) / 1662u)


#endif	/* MAIN_H */

