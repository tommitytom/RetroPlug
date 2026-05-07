#include "sunsoft.h"

/*
 * Sunsoft 5B (AY-3-8910 compatible) frequency table.
 *
 * Tone period = CPU_clock / (16 * frequency)
 * CPU_clock = 1789773 Hz (NTSC NES)
 * 12-bit period register (0-4095).
 *
 * Valid MIDI range: 21-127
 */
static const u16 s5b_timer_lut[128] = {
        0,     0,     0,     0,     0,     0,     0,     0,  /*   0-  7 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*   8- 15 */
        0,     0,     0,     0,     0,  4067,  3838,  3623,  /*  16- 23 */
     3419,  3228,  3046,  2875,  2714,  2561,  2418,  2282,  /*  24- 31 */
     2154,  2033,  1919,  1811,  1709,  1613,  1523,  1437,  /*  32- 39 */
     1356,  1280,  1208,  1140,  1076,  1016,   959,   905,  /*  40- 47 */
      854,   806,   761,   718,   678,   640,   604,   570,  /*  48- 55 */
      538,   507,   479,   452,   427,   403,   380,   359,  /*  56- 63 */
      338,   319,   301,   284,   268,   253,   239,   225,  /*  64- 71 */
      213,   201,   189,   179,   169,   159,   150,   142,  /*  72- 79 */
      134,   126,   119,   112,   106,   100,    94,    89,  /*  80- 87 */
       84,    79,    75,    70,    66,    63,    59,    56,  /*  88- 95 */
       52,    49,    47,    44,    41,    39,    37,    35,  /*  96-103 */
       33,    31,    29,    27,    26,    24,    23,    21,  /* 104-111 */
       20,    19,    18,    17,    16,    15,    14,    13,  /* 112-119 */
       12,    12,    11,    10,    10,     9,     8,     8,  /* 120-127 */
};

/* ===== Per-channel Sunsoft 5B state ===== */

static u8 _s5bVol[S5B_CH_COUNT];         /* 4-bit volume per channel */
static u8 _s5bNoteActive[S5B_CH_COUNT];
static u8 _s5bMixer;                     /* mixer register shadow */

/* ===== Sunsoft 5B register write helper ===== */

static void s5b_write(u8 reg, u8 val)
{
	S5B_ADDR = reg;
	S5B_DATA = val;
}

/* ===== Sunsoft 5B note on / off ===== */

void s5b_note_on(u8 ch, u8 note, u8 vel)
{
	u16 t;
	u8 reg_lo, reg_hi;

	if (ch >= S5B_CH_COUNT) return;
	if (note > 127) return;
	t = s5b_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	reg_lo = ch * 2;       /* 0x00, 0x02, 0x04 */
	reg_hi = ch * 2 + 1;   /* 0x01, 0x03, 0x05 */

	_s5bVol[ch]        = vel >> 3;
	_s5bNoteActive[ch] = 1;

	/* Enable tone, disable noise for this channel */
	_s5bMixer &= ~(1 << ch);       /* clear tone disable bit (active low) */
	_s5bMixer |= (1 << (ch + 3));  /* set noise disable bit */

	s5b_write(reg_lo, (u8)(t & 0xFF));
	s5b_write(reg_hi, (u8)((t >> 8) & 0x0F));
	s5b_write(S5B_REG_A_VOL + ch, _s5bVol[ch]);
	s5b_write(S5B_REG_MIXER, _s5bMixer);
}

void s5b_note_off(u8 ch)
{
	if (ch >= S5B_CH_COUNT) return;
	_s5bNoteActive[ch] = 0;

	/* Set volume to 0 */
	s5b_write(S5B_REG_A_VOL + ch, 0);
}

/* ===== Sunsoft 5B CC handler ===== */

void handle_cc_s5b(u8 ch, u8 cc, u8 val)
{
	if (ch >= S5B_CH_COUNT) return;

	switch (cc) {
	case CC_MOD_WHEEL:
		/* Use mod wheel to toggle noise mixing.
		 * val 0-63: tone only, val 64-127: tone + noise */
		if (val >= 64) {
			_s5bMixer &= ~(1 << (ch + 3)); /* enable noise */
		} else {
			_s5bMixer |= (1 << (ch + 3));  /* disable noise */
		}
		s5b_write(S5B_REG_MIXER, _s5bMixer);
		break;

	case CC_VOLUME:
		_s5bVol[ch] = val >> 3;
		if (_s5bNoteActive[ch]) {
			s5b_write(S5B_REG_A_VOL + ch, _s5bVol[ch]);
		}
		break;

	case CC_ALL_NOTES_OFF:
		s5b_note_off(ch);
		break;
	}
}

/* ===== Sunsoft 5B initialisation ===== */

void s5b_init(void)
{
	u8 i;

	/* Disable all tone and noise outputs (all bits high = all disabled) */
	_s5bMixer = 0x3F;
	s5b_write(S5B_REG_MIXER, _s5bMixer);

	for (i = 0; i < S5B_CH_COUNT; i++) {
		_s5bVol[i]        = 15;
		_s5bNoteActive[i] = 0;

		/* Silence channel */
		s5b_write(S5B_REG_A_VOL + i, 0);
	}
}
