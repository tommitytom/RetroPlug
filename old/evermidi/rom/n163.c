#include "n163.h"

/*
 * Namco 163 frequency table.
 *
 * N163 frequency formula:
 *   f = (freq_reg * clock) / (15 * 65536 * num_channels)
 *   clock = 1789773 Hz
 *   With 4 channels: f = freq_reg * 1789773 / (15 * 65536 * 4)
 *                      = freq_reg * 0.4547...
 *   So freq_reg = f / 0.4547 = f * 2.1993...
 *
 * We use an 18-bit frequency register (3 bytes).
 * Valid MIDI range: 21-127
 *
 * Stored as u32 (only low 18 bits used).
 */
static const u32 n163_freq_lut[128] = {
        0,      0,      0,      0,      0,      0,      0,      0,  /*   0-  7 */
        0,      0,      0,      0,      0,      0,      0,      0,  /*   8- 15 */
        0,      0,      0,      0,      0,                           /*  16- 20 */
       61,     64,     68,     72,     77,     81,                   /*  21- 26 */
       86,     91,     96,    102,    108,    115,                   /*  27- 32 */
      121,    129,    136,    145,    153,    162,                   /*  33- 38 */
      172,    182,    193,    204,    217,    229,                   /*  39- 44 */
      243,    257,    273,    289,    306,    324,                   /*  45- 50 */
      344,    364,    386,    409,    433,    459,                   /*  51- 56 */
      486,    515,    546,    578,    613,    649,                   /*  57- 62 */
      688,    729,    772,    818,    867,    918,                   /*  63- 68 */
      973,   1031,   1092,   1157,   1226,   1298,                  /*  69- 74 */
     1375,   1457,   1544,   1636,   1733,   1836,                  /*  75- 80 */
     1945,   2061,   2183,   2313,   2451,   2597,                  /*  81- 86 */
     2751,   2914,   3088,   3271,   3466,   3672,                  /*  87- 92 */
     3890,   4122,   4367,   4627,   4902,   5193,                  /*  93- 98 */
     5502,   5829,   6175,   6542,   6932,   7344,                  /*  99-104 */
     7780,   8241,   8733,   9253,   9804,  10387,                  /* 105-110 */
    11005,   11659,  12351,  13085,  13864,  14689,                  /* 111-116 */
    15561,  16486,  17467,  18506,  19609,  20774,                  /* 117-122 */
    22010,  23318,  24702,  26170,  27728,                          /* 123-127 */
};

/* Default waveform: 32-sample sine wave stored in N163 wave RAM.
 * Each byte holds two 4-bit samples (high nibble first).
 * 32 samples = 16 bytes. */
static const u8 default_wave[16] = {
	0x89, 0xAB, 0xCD, 0xEE, 0xFE, 0xED, 0xCB, 0xA9,
	0x76, 0x54, 0x32, 0x11, 0x01, 0x12, 0x34, 0x56,
};

/* ===== Per-channel N163 state ===== */

static u8 _n163Vol[N163_NUM_CHANNELS];
static u8 _n163NoteActive[N163_NUM_CHANNELS];
static u8 _n163WaveAddr[N163_NUM_CHANNELS]; /* wave RAM start addr per channel */

/* ===== N163 register read/write helpers ===== */

static void n163_write_reg(u8 addr, u8 val)
{
	/* Set address with auto-increment disabled (bit 7 = 1) */
	N163_ADDR = 0x80 | (addr & 0x7F);
	N163_DATA = val;
}

static void n163_write_wave(u8 addr, const u8 *data, u8 len)
{
	u8 i;
	/* Set address with auto-increment enabled (bit 7 = 0) */
	N163_ADDR = addr & 0x7F;
	for (i = 0; i < len; i++) {
		N163_DATA = data[i];
	}
}

/* Get the register base address for a channel index (0-based).
 * With N163_NUM_CHANNELS=4, channel 0 starts at $60, channel 3 at $78. */
static u8 ch_reg_base(u8 ch)
{
	return 0x78 - ((N163_NUM_CHANNELS - 1 - ch) * 8);
}

/* ===== N163 note on / off ===== */

void n163_note_on(u8 ch, u8 note, u8 vel)
{
	u32 freq;
	u8 base;

	if (ch >= N163_NUM_CHANNELS) return;
	if (note > 127) return;
	freq = n163_freq_lut[note];
	if (freq == 0) return;
	if (g_is_pal) freq = PAL_SCALE_FREQ(freq);

	base = ch_reg_base(ch);

	_n163Vol[ch]        = vel >> 3;
	_n163NoteActive[ch] = 1;

	/* Clear phase accumulator */
	n163_write_reg(base + 1, 0);
	n163_write_reg(base + 3, 0);
	n163_write_reg(base + 5, 0);

	/* Frequency low */
	n163_write_reg(base + 0, (u8)(freq & 0xFF));
	/* Frequency mid */
	n163_write_reg(base + 2, (u8)((freq >> 8) & 0xFF));
	/* Frequency high [1:0] + wave length bits [4:2]
	 * Wave length = 32 samples -> encoded as (256 - 32*4)/4 shifted = 0x1C << 2 -> actually:
	 * The length field in bits [4:2] encodes 256 - (length * 4).
	 * For 32 samples: 256 - 128 = 128 -> bits [7:2] of the byte = 128 >> 2 = 0x20
	 * Actually: reg = (freq_hi & 0x03) | ((256 - wavelength * 4) & 0xFC)
	 * For 32 samples: 256 - 128 = 128 = 0x80 */
	n163_write_reg(base + 4, (u8)((freq >> 16) & 0x03) | 0x80);

	/* Wave address */
	n163_write_reg(base + 6, _n163WaveAddr[ch]);

	/* Volume (and channel count for the last channel register $7F) */
	if (base + 7 == 0x7F) {
		/* Register $7F also controls number of active channels.
		 * bits [6:4] = number of extra channels (0 = 1 ch, 3 = 4 ch, 7 = 8 ch) */
		n163_write_reg(base + 7, ((N163_NUM_CHANNELS - 1) << 4) | _n163Vol[ch]);
	} else {
		n163_write_reg(base + 7, _n163Vol[ch]);
	}
}

void n163_note_off(u8 ch)
{
	u8 base;

	if (ch >= N163_NUM_CHANNELS) return;
	_n163NoteActive[ch] = 0;

	base = ch_reg_base(ch);

	/* Silence by setting volume to 0 */
	if (base + 7 == 0x7F) {
		n163_write_reg(base + 7, ((N163_NUM_CHANNELS - 1) << 4));
	} else {
		n163_write_reg(base + 7, 0);
	}

	/* Zero frequency to stop phase accumulation */
	n163_write_reg(base + 0, 0);
	n163_write_reg(base + 2, 0);
	n163_write_reg(base + 4, 0x80); /* keep wave length, zero freq */
}

/* ===== N163 CC handler ===== */

void handle_cc_n163(u8 ch, u8 cc, u8 val)
{
	if (ch >= N163_NUM_CHANNELS) return;

	switch (cc) {
	case CC_MOD_WHEEL:
		/* Mod wheel: shift wave address.
		 * 4 waveforms stored at offsets 0, 16, 32, 48 in wave RAM.
		 * val 0-31 -> wave 0, 32-63 -> wave 1, 64-95 -> wave 2, 96-127 -> wave 3 */
		_n163WaveAddr[ch] = (val >> 5) * 16;
		if (_n163NoteActive[ch]) {
			n163_write_reg(ch_reg_base(ch) + 6, _n163WaveAddr[ch]);
		}
		break;

	case CC_VOLUME:
		_n163Vol[ch] = val >> 3;
		if (_n163NoteActive[ch]) {
			u8 base = ch_reg_base(ch);
			if (base + 7 == 0x7F) {
				n163_write_reg(base + 7, ((N163_NUM_CHANNELS - 1) << 4) | _n163Vol[ch]);
			} else {
				n163_write_reg(base + 7, _n163Vol[ch]);
			}
		}
		break;

	case CC_ALL_NOTES_OFF:
		n163_note_off(ch);
		break;
	}
}

/* ===== N163 initialisation ===== */

void n163_init(void)
{
	u8 i, base;

	/* Load default waveform into wave RAM at address 0.
	 * We load 4 copies (at offsets 0, 16, 32, 48) for wave switching. */
	n163_write_wave(0x00, default_wave, 16);
	n163_write_wave(0x10, default_wave, 16);
	n163_write_wave(0x20, default_wave, 16);
	n163_write_wave(0x30, default_wave, 16);

	/* Set number of active channels in register $7F */
	n163_write_reg(0x7F, ((N163_NUM_CHANNELS - 1) << 4));

	for (i = 0; i < N163_NUM_CHANNELS; i++) {
		_n163Vol[i]        = 15;
		_n163NoteActive[i] = 0;
		_n163WaveAddr[i]   = 0;

		base = ch_reg_base(i);

		/* Silence channel */
		n163_write_reg(base + 0, 0);  /* freq low */
		n163_write_reg(base + 2, 0);  /* freq mid */
		n163_write_reg(base + 4, 0x80); /* freq high + wave len */
		n163_write_reg(base + 6, 0);  /* wave addr */
		if (base + 7 == 0x7F) {
			n163_write_reg(base + 7, ((N163_NUM_CHANNELS - 1) << 4));
		} else {
			n163_write_reg(base + 7, 0); /* vol=0 */
		}
	}
}
