#include "vrc7.h"

/*
 * VRC7 (Konami clone of YM2413 OPLL) frequency table.
 *
 * The VRC7 uses a 9-bit F-Number + 3-bit octave to set pitch.
 * Frequency = (F * 49716) / (2^(19 - octave))
 *
 * We store octave in the high byte [6:4] and the 9-bit fnum split
 * across both bytes: low byte = fnum[7:0], high byte bit 0 = fnum[8].
 *
 * This table covers MIDI notes 21-127. Each entry is:
 *   low byte  = fnum low 8 bits
 *   high byte = (octave << 1) | fnum_hi_bit
 *
 * The caller shifts octave into [3:1] and fnum_hi into [0] for reg $2x.
 */

/* Pre-computed fnum + octave for MIDI notes 0-127.
 * Format: ((octave & 7) << 9) | (fnum & 0x1FF)
 * Stored as u16; caller unpacks.
 *
 * Octave boundaries align to C notes:
 *   octave 0 = MIDI 12-23  (C0-B0)
 *   octave 1 = MIDI 24-35  (C1-B1)
 *   ...
 *   octave 7 = MIDI 96-107 (C7-B7)
 *   MIDI 108+ clamped to octave 7.
 *
 * Base F-Numbers (semitone 0-11 = C..B):
 *   173, 183, 194, 206, 218, 231, 244, 258, 273, 289, 305, 323
 */
static const u16 vrc7_freq_lut[128] = {
        0,     0,     0,     0,     0,     0,     0,     0,  /*   0-  7 */
        0,     0,     0,     0,                               /*   8- 11 */
    /* octave 0 : MIDI 12-23 (C0-B0) */
    0x00AD, 0x00B7, 0x00C2, 0x00CE, 0x00DA, 0x00E7,          /*  12- 17 */
    0x00F4, 0x0102, 0x0111, 0x0121, 0x0131, 0x0143,          /*  18- 23 */
    /* octave 1 : MIDI 24-35 (C1-B1) */
    0x02AD, 0x02B7, 0x02C2, 0x02CE, 0x02DA, 0x02E7,          /*  24- 29 */
    0x02F4, 0x0302, 0x0311, 0x0321, 0x0331, 0x0343,          /*  30- 35 */
    /* octave 2 : MIDI 36-47 (C2-B2) */
    0x04AD, 0x04B7, 0x04C2, 0x04CE, 0x04DA, 0x04E7,          /*  36- 41 */
    0x04F4, 0x0502, 0x0511, 0x0521, 0x0531, 0x0543,          /*  42- 47 */
    /* octave 3 : MIDI 48-59 (C3-B3) */
    0x06AD, 0x06B7, 0x06C2, 0x06CE, 0x06DA, 0x06E7,          /*  48- 53 */
    0x06F4, 0x0702, 0x0711, 0x0721, 0x0731, 0x0743,          /*  54- 59 */
    /* octave 4 : MIDI 60-71 (C4-B4) */
    0x08AD, 0x08B7, 0x08C2, 0x08CE, 0x08DA, 0x08E7,          /*  60- 65 */
    0x08F4, 0x0902, 0x0911, 0x0921, 0x0931, 0x0943,          /*  66- 71 */
    /* octave 5 : MIDI 72-83 (C5-B5) */
    0x0AAD, 0x0AB7, 0x0AC2, 0x0ACE, 0x0ADA, 0x0AE7,          /*  72- 77 */
    0x0AF4, 0x0B02, 0x0B11, 0x0B21, 0x0B31, 0x0B43,          /*  78- 83 */
    /* octave 6 : MIDI 84-95 (C6-B6) */
    0x0CAD, 0x0CB7, 0x0CC2, 0x0CCE, 0x0CDA, 0x0CE7,          /*  84- 89 */
    0x0CF4, 0x0D02, 0x0D11, 0x0D21, 0x0D31, 0x0D43,          /*  90- 95 */
    /* octave 7 : MIDI 96-107 (C7-B7) */
    0x0EAD, 0x0EB7, 0x0EC2, 0x0ECE, 0x0EDA, 0x0EE7,          /*  96-101 */
    0x0EF4, 0x0F02, 0x0F11, 0x0F21, 0x0F31, 0x0F43,          /* 102-107 */
    /* MIDI 108-119: clamp to octave 7 */
    0x0EAD, 0x0EB7, 0x0EC2, 0x0ECE, 0x0EDA, 0x0EE7,          /* 108-113 */
    0x0EF4, 0x0F02, 0x0F11, 0x0F21, 0x0F31, 0x0F43,          /* 114-119 */
    /* MIDI 120-127: clamp to octave 7 */
    0x0EAD, 0x0EB7, 0x0EC2, 0x0ECE, 0x0EDA, 0x0EE7,          /* 120-125 */
    0x0EF4, 0x0F02,                                           /* 126-127 */
};

/* ===== VRC7 custom patch shadow ($00-$07) ===== */
/* Default: fast attack, no decay, sustained — a clean FM organ tone.
 * CC 102-109 write directly to these registers at runtime. */
static u8 _vrc7Patch[8] = {
    0x00,   /* $00: Mod TVSK MMMM  — mult=0 (=1x), no T/V/S/K */
    0x00,   /* $01: Car TVSK MMMM  — mult=0 (=1x), no T/V/S/K */
    0x00,   /* $02: Mod KK + output level=0 (max modulation depth) */
    0x00,   /* $03: Car KK, waveforms=sine/sine, feedback=0 */
    0xA0,   /* $04: Mod attack=10, decay=0 (sustains after attack) */
    0xA0,   /* $05: Car attack=10, decay=0 (sustains after attack) */
    0x00,   /* $06: Mod sustain level=0, release=0 */
    0x00,   /* $07: Car sustain level=0, release=0 */
};

/* ===== Per-channel VRC7 state ===== */

static u8 _vrc7Inst[VRC7_CH_COUNT];       /* instrument patch 0-15 */
static u8 _vrc7Vol[VRC7_CH_COUNT];        /* channel volume 0-15 (CC7, inverted: 0=loud) */
static u8 _vrc7NoteVol[VRC7_CH_COUNT];    /* effective note volume (CC7 + velocity) */
static u8 _vrc7NoteActive[VRC7_CH_COUNT];

/* ===== VRC7 register write helper ===== */

static void vrc7_write(u8 reg, u8 val)
{
	/* The VRC7 requires a short delay between address and data writes.
	 * 6 CPU cycles minimum (~3.3 us at 1.79 MHz). A few NOPs suffice
	 * since cc65 function-call overhead already burns some. */
	VRC7_ADDR = reg;
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");
	VRC7_DATA = val;
	/* Need ~84 cycles after data write before next register write */
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop"); __asm__("nop"); __asm__("nop");
	__asm__("nop"); __asm__("nop");
}

/* ===== VRC7 note on / off ===== */

void vrc7_note_on(u8 ch, u8 note, u8 vel)
{
	u16 raw;
	u8 fnum_lo, fnum_hi, octave;

	if (ch >= VRC7_CH_COUNT) return;
	if (note > 127) return;
	raw = vrc7_freq_lut[note];
	if (raw == 0) return;

	if (g_is_pal) {
		/* VRC7 ref clock = CPU_clock/36; on PAL it's lower, so F-number
		 * must be scaled up by NTSC_clock/PAL_clock to keep same pitch. */
		u16 fnum = raw & 0x1FF;
		fnum = (u16)PAL_SCALE_FREQ(fnum);
		if (fnum > 0x1FF) fnum = 0x1FF;
		raw = (raw & ~0x1FFu) | fnum;
	}

	fnum_lo = (u8)(raw & 0xFF);
	fnum_hi = (raw >> 8) & 0x01;
	octave  = (u8)((raw >> 9) & 0x07);

	_vrc7NoteActive[ch] = 1;

	/* Combine CC7 channel volume with note velocity.
	 * VRC7 attenuation is 4-bit (0=loud, 15=silent).
	 * Scale: vel 0→127 maps to attenuation 15→0, then add CC7 attenuation,
	 * clamped to 15. This means CC7 at max (val=127→atten=0) gives full
	 * velocity control; lower CC7 values raise the attenuation floor. */
	{
		u8 velAtten = (u8)((127 - vel) >> 3);   /* 0..15 */
		u8 vol = _vrc7Vol[ch] + velAtten;
		if (vol > 15) vol = 15;
		_vrc7NoteVol[ch] = vol;
	}

	/* Key-off first so the VRC7 sees a 0→1 transition on the key-on
	 * bit, which restarts the envelope.  Without this, back-to-back
	 * notes inherit the previous envelope position → inconsistent volume. */
	vrc7_write(0x20 + ch, 0x20 | (octave << 1) | fnum_hi);

	/* $3x: instrument | volume */
	vrc7_write(0x30 + ch, (_vrc7Inst[ch] << 4) | _vrc7NoteVol[ch]);

	/* $1x: F-Number low */
	vrc7_write(0x10 + ch, fnum_lo);

	/* $2x: sustain=1, key-on=1, octave[2:0], fnum_hi */
	vrc7_write(0x20 + ch, 0x30 | (octave << 1) | fnum_hi);
}

void vrc7_note_off(u8 ch)
{
	u8 reg20;

	if (ch >= VRC7_CH_COUNT) return;
	_vrc7NoteActive[ch] = 0;

	/* Clear key-on bit (bit 4), keep sustain=1 for release */
	reg20 = 0x20;  /* sustain=1, key-off, zero octave/fnum is fine for release */
	vrc7_write(0x20 + ch, reg20);
}

/* ===== VRC7 CC handler ===== */

void handle_cc_vrc7(u8 ch, u8 cc, u8 val)
{
	if (ch >= VRC7_CH_COUNT) return;

	/* Custom patch registers — global, any VRC7 channel can write them.
	 * CC 102 → reg $00 (mod TVSK+mult)
	 * CC 103 → reg $01 (car TVSK+mult)
	 * CC 104 → reg $02 (mod key scaling + output level)
	 * CC 105 → reg $03 (car key scaling, waveforms, feedback)
	 * CC 106 → reg $04 (mod attack + decay)
	 * CC 107 → reg $05 (car attack + decay)
	 * CC 108 → reg $06 (mod sustain + release)
	 * CC 109 → reg $07 (car sustain + release)
	 * MIDI value 0-127 maps directly to the lower 7 bits of the register. */
	if (cc >= CC_PATCH_REG0 && cc <= CC_PATCH_REG7) {
		u8 reg = cc - CC_PATCH_REG0;
		_vrc7Patch[reg] = val;
		vrc7_write(reg, val);
		return;
	}

	switch (cc) {
	case CC_MOD_WHEEL:
		/* Use mod wheel to select instrument patch 0-15 */
		_vrc7Inst[ch] = val >> 3;
		if (_vrc7Inst[ch] > 15) _vrc7Inst[ch] = 15;
		if (_vrc7NoteActive[ch]) {
			vrc7_write(0x30 + ch, (_vrc7Inst[ch] << 4) | _vrc7Vol[ch]);
		}
		break;

	case CC_VOLUME:
		_vrc7Vol[ch] = 15 - (val >> 3);
		if (_vrc7NoteActive[ch]) {
			vrc7_write(0x30 + ch, (_vrc7Inst[ch] << 4) | _vrc7NoteVol[ch]);
		}
		break;

	case CC_ALL_NOTES_OFF:
		vrc7_note_off(ch);
		break;
	}
}

/* ===== VRC7 initialisation ===== */

void vrc7_init(void)
{
	u8 i;

	/* Write default custom patch to registers $00-$07 */
	for (i = 0; i < 8; i++) {
		vrc7_write(i, _vrc7Patch[i]);
	}

	/* Silence all 6 channels */
	for (i = 0; i < VRC7_CH_COUNT; i++) {
		_vrc7Inst[i]       = 0;   /* custom instrument */
		_vrc7Vol[i]        = 0;   /* loudest */
		_vrc7NoteVol[i]    = 0;   /* loudest */
		_vrc7NoteActive[i] = 0;

		vrc7_write(0x20 + i, 0x00);  /* key off */
		vrc7_write(0x30 + i, 0x0F);  /* silent volume */
	}
}
