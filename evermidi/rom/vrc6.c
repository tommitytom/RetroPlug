#include "vrc6.h"

/* VRC6 Pulse: f = 1789773 / (16 * (t + 1)), 12-bit timer, valid MIDI 21-127 */
static const u16 vrc6_pulse_timer_lut[128] = {
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

/* VRC6 Sawtooth: f = 1789773 / (14 * (t + 1)), 12-bit timer, valid MIDI 24-127 */
static const u16 vrc6_saw_timer_lut[128] = {
        0,     0,     0,     0,     0,     0,     0,     0,  /*   0-  7 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*   8- 15 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*  16- 23 */
     3908,  3689,  3482,  3286,  3102,  2928,  2763,  2608,  /*  24- 31 */
     2462,  2323,  2193,  2070,  1954,  1844,  1740,  1643,  /*  32- 39 */
     1550,  1463,  1381,  1304,  1230,  1161,  1096,  1034,  /*  40- 47 */
      976,   921,   870,   821,   775,   731,   690,   651,  /*  48- 55 */
      615,   580,   547,   517,   488,   460,   434,   410,  /*  56- 63 */
      387,   365,   345,   325,   307,   290,   273,   258,  /*  64- 71 */
      243,   230,   217,   204,   193,   182,   172,   162,  /*  72- 79 */
      153,   144,   136,   128,   121,   114,   108,   102,  /*  80- 87 */
       96,    91,    85,    81,    76,    72,    68,    64,  /*  88- 95 */
       60,    57,    53,    50,    47,    45,    42,    40,  /*  96-103 */
       37,    35,    33,    31,    30,    28,    26,    25,  /* 104-111 */
       23,    22,    21,    19,    18,    17,    16,    15,  /* 112-119 */
       14,    13,    13,    12,    11,    10,    10,     9,  /* 120-127 */
};

/* ===== Per-channel VRC6 state ===== */

static u8 _vrc6Duty[2];        /* 0-7 duty cycle */
static u8 _vrc6PVol[2];        /* 4-bit volume */
static u8 _vrc6SawRate;        /* accumulator rate 0-42 */
static u8 _vrc6NoteActive[3];  /* 0=P1, 1=P2, 2=Saw */

/* ===== VRC6 register helpers ===== */

/*
 * VRC6 pulse control byte ($9000/$A000):
 *   bit  7   = mode (0 = normal pulse, 1 = digital/constant)
 *   bits 6-4 = duty cycle (0-7)
 *   bits 3-0 = volume
 */
static u8 vrc6_pulse_ctrl(u8 ch)
{
	return (_vrc6Duty[ch] << 4) | _vrc6PVol[ch];
}

/* ===== VRC6 channel note-on / note-off ===== */

void vrc6_p1_note_on(u8 note, u8 vel)
{
	u16 t;
	if (note > 127) return;
	t = vrc6_pulse_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	_vrc6PVol[0]       = vel >> 3;
	_vrc6NoteActive[0] = 1;

	VRC6_P1_CTRL = vrc6_pulse_ctrl(0);
	VRC6_P1_LO   = (u8)(t);
	VRC6_P1_HI   = 0x80 | (u8)(t >> 8);
}

void vrc6_p1_note_off(void)
{
	_vrc6NoteActive[0] = 0;
	VRC6_P1_CTRL = (_vrc6Duty[0] << 4);
}

void vrc6_p2_note_on(u8 note, u8 vel)
{
	u16 t;
	if (note > 127) return;
	t = vrc6_pulse_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	_vrc6PVol[1]       = vel >> 3;
	_vrc6NoteActive[1] = 1;

	VRC6_P2_CTRL = vrc6_pulse_ctrl(1);
	VRC6_P2_LO   = (u8)(t);
	VRC6_P2_HI   = 0x80 | (u8)(t >> 8);
}

void vrc6_p2_note_off(void)
{
	_vrc6NoteActive[1] = 0;
	VRC6_P2_CTRL = (_vrc6Duty[1] << 4);
}

void vrc6_saw_note_on(u8 note, u8 vel)
{
	u16 t;
	if (note > 127) return;
	t = vrc6_saw_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	/* scale velocity to saw rate: 0-127 -> 0-42 */
	_vrc6SawRate       = (u8)(((u16)vel * SAW_MAX_RATE) / 127);
	_vrc6NoteActive[2] = 1;

	VRC6_SAW_RATE = _vrc6SawRate;
	VRC6_SAW_LO   = (u8)(t);
	VRC6_SAW_HI   = 0x80 | (u8)(t >> 8);
}

void vrc6_saw_note_off(void)
{
	_vrc6NoteActive[2] = 0;
	VRC6_SAW_RATE = 0;
}

/* ===== VRC6 CC handlers ===== */

void handle_cc_vrc6_pulse(u8 ch, u8 cc, u8 val)
{
	/* ch: 0 = VRC6 Pulse 1, 1 = VRC6 Pulse 2 */
	switch (cc) {
	case CC_MOD_WHEEL:
		/* VRC6 pulse has 8 duty levels (0-7) */
		_vrc6Duty[ch] = val >> 4;
		if (_vrc6Duty[ch] > 7) _vrc6Duty[ch] = 7;
		if (_vrc6NoteActive[ch]) {
			if (ch == 0) VRC6_P1_CTRL = vrc6_pulse_ctrl(0);
			else         VRC6_P2_CTRL = vrc6_pulse_ctrl(1);
		}
		break;

	case CC_VOLUME:
		_vrc6PVol[ch] = val >> 3;
		if (_vrc6NoteActive[ch]) {
			if (ch == 0) VRC6_P1_CTRL = vrc6_pulse_ctrl(0);
			else         VRC6_P2_CTRL = vrc6_pulse_ctrl(1);
		}
		break;

	case CC_ALL_NOTES_OFF:
		if (ch == 0) vrc6_p1_note_off();
		else         vrc6_p2_note_off();
		break;
	}
}

void handle_cc_vrc6_saw(u8 cc, u8 val)
{
	switch (cc) {
	case CC_VOLUME:
		_vrc6SawRate = (u8)(((u16)val * SAW_MAX_RATE) / 127);
		if (_vrc6NoteActive[2]) {
			VRC6_SAW_RATE = _vrc6SawRate;
		}
		break;

	case CC_ALL_NOTES_OFF:
		vrc6_saw_note_off();
		break;
	}
}

/* ===== VRC6 initialisation ===== */

void vrc6_init(void)
{
	VRC6_FREQ_CTRL = 0x00;   /* no halt, no frequency scaling */
	_vrc6Duty[0] = 3; _vrc6Duty[1] = 3;   /* ~50% duty (4/8 steps high) */
	_vrc6PVol[0] = 15; _vrc6PVol[1] = 15;
	_vrc6SawRate = SAW_MAX_RATE;
	_vrc6NoteActive[0] = 0;
	_vrc6NoteActive[1] = 0;
	_vrc6NoteActive[2] = 0;

	/* Silence VRC6 channels */
	VRC6_P1_CTRL = 0;
	VRC6_P1_HI   = 0;
	VRC6_P2_CTRL = 0;
	VRC6_P2_HI   = 0;
	VRC6_SAW_RATE = 0;
	VRC6_SAW_HI  = 0;
}
