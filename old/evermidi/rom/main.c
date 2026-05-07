#include "main.h"

#ifdef USE_VRC6
#include "vrc6.h"
#elif defined(USE_VRC7)
#include "vrc7.h"
#elif defined(USE_S5B)
#include "sunsoft.h"
#elif defined(USE_N163)
#include "n163.h"
#endif

/* ===== NES APU register definitions ===== */

/* Pulse 1 ($4000-$4003) */
#define APU_SQ1_VOL    (*(volatile u8 *)0x4000)
#define APU_SQ1_SWEEP  (*(volatile u8 *)0x4001)
#define APU_SQ1_LO     (*(volatile u8 *)0x4002)
#define APU_SQ1_HI     (*(volatile u8 *)0x4003)

/* Pulse 2 ($4004-$4007) */
#define APU_SQ2_VOL    (*(volatile u8 *)0x4004)
#define APU_SQ2_SWEEP  (*(volatile u8 *)0x4005)
#define APU_SQ2_LO     (*(volatile u8 *)0x4006)
#define APU_SQ2_HI     (*(volatile u8 *)0x4007)

/* Triangle ($4008-$400B) */
#define APU_TRI_LINEAR (*(volatile u8 *)0x4008)
#define APU_TRI_LO     (*(volatile u8 *)0x400A)
#define APU_TRI_HI     (*(volatile u8 *)0x400B)

/* Noise ($400C-$400F) */
#define APU_NOISE_VOL  (*(volatile u8 *)0x400C)
#define APU_NOISE_LO   (*(volatile u8 *)0x400E)
#define APU_NOISE_HI   (*(volatile u8 *)0x400F)

/* Control / frame counter */
#define APU_STATUS     (*(volatile u8 *)0x4015)
#define APU_FRAME      (*(volatile u8 *)0x4017)

/* DMC ($4010-$4013) */
#define APU_DMC_CTRL   (*(volatile u8 *)0x4010)  /* IRQ enable, loop, rate index */
#define APU_DMC_DAC    (*(volatile u8 *)0x4011)  /* 7-bit DAC direct load */
#define APU_DMC_ADDR   (*(volatile u8 *)0x4012)  /* sample addr = $C000 + val*64 */
#define APU_DMC_LEN    (*(volatile u8 *)0x4013)  /* sample len  = val*16 + 1 */

/* ===== DMC sample bank PRG address =====
 *
 * ed_cmd_file_read_mem() DMAs sample data to this system bus address.
 * The 32K system ROM at $8000-$FFFF lives at the end of the 128K OS PRG area:
 *   absolute = ADDR_OS_PRG + 0x18000
 *   $C000    = absolute + 0x4000
 *
 * Your code + rodata must fit in $8000-$BFFF so $C000-$FFFF is free.
 * >>> ADJUST THIS if your mapper setup differs. <<<
 */
#define DMC_PRG_ADDR    (ADDR_OS_PRG + 0x1C000)
#define DMC_BANK_SIZE   16384
#define DMC_VEC_SIZE    6  /* NMI/RESET/IRQ vectors at $FFFA-$FFFF */


/* ===== MIDI channel -> NES channel mapping =====
 *
 *   MIDI ch 1  (0x00) -> APU Pulse 1
 *   MIDI ch 2  (0x01) -> APU Pulse 2
 *   MIDI ch 3  (0x02) -> APU Triangle
 *   MIDI ch 4  (0x03) -> APU Noise
 *   MIDI ch 5  (0x04) -> (reserved for samples)
 *   MIDI ch 6+ -> expansion audio (mapper dependent)
 */

#define MIDI_CH_PULSE1     0x00
#define MIDI_CH_PULSE2     0x01
#define MIDI_CH_TRIANGLE   0x02
#define MIDI_CH_NOISE      0x03
#define MIDI_CH_DMC        0x04

void midiRead(void);


/* ===== PAL/NTSC detection ===== */

u8 g_is_pal;

/* Implemented in crt0.s: counts CPU cycles between vblanks.
 * Returns 0 = NTSC, 1 = PAL NES, 2 = Dendy */
u8 __fastcall__ getTVSystem(void);

/* ===== Lookup tables ===== */

/* APU Pulse: f = 1789773 / (16 * (t + 1)), 11-bit timer, valid MIDI 33-127 */
static const u16 pulse_timer_lut[128] = {
        0,     0,     0,     0,     0,     0,     0,     0,  /*   0-  7 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*   8- 15 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*  16- 23 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*  24- 31 */
        0,  2033,  1919,  1811,  1709,  1613,  1523,  1437,  /*  32- 39 */
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

/* APU Triangle: f = 1789773 / (32 * (t + 1)), 11-bit timer, valid MIDI 21-127 */
static const u16 tri_timer_lut[128] = {
        0,     0,     0,     0,     0,     0,     0,     0,  /*   0-  7 */
        0,     0,     0,     0,     0,     0,     0,     0,  /*   8- 15 */
        0,     0,     0,     0,     0,  2033,  1919,  1811,  /*  16- 23 */
     1709,  1613,  1523,  1437,  1356,  1280,  1208,  1140,  /*  24- 31 */
     1076,  1016,   959,   905,   854,   806,   761,   718,  /*  32- 39 */
      678,   640,   604,   570,   538,   507,   479,   452,  /*  40- 47 */
      427,   403,   380,   359,   338,   319,   301,   284,  /*  48- 55 */
      268,   253,   239,   225,   213,   201,   189,   179,  /*  56- 63 */
      169,   159,   150,   142,   134,   126,   119,   112,  /*  64- 71 */
      106,   100,    94,    89,    84,    79,    75,    70,  /*  72- 79 */
       66,    63,    59,    56,    52,    49,    47,    44,  /*  80- 87 */
       41,    39,    37,    35,    33,    31,    29,    27,  /*  88- 95 */
       26,    24,    23,    21,    20,    19,    18,    17,  /*  96-103 */
       16,    15,    14,    13,    12,    12,    11,    10,  /* 104-111 */
       10,     9,     8,     8,     7,     7,     6,     6,  /* 112-119 */
        6,     5,     5,     5,     4,     4,     4,     3,  /* 120-127 */
};

/* 32 distinct NES noise timbres, mapped to MIDI 36-67.
 *   36-51: normal mode,   period 15->0  (low to high pitch)
 *   52-67: metallic mode, period 15->0  (low to high pitch) */
static const u8 noise_lut[32] = {
    0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,  /* normal:   low ... */
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,  /*           ... high */
    0x8F, 0x8E, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x88,  /* metallic: low ... */
    0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80,  /*           ... high */
};

#define NOISE_BASE_NOTE  36
#define NOISE_NOTE_COUNT 32

/* ===== Per-channel state ===== */

/* Channel indices for _lastNotes / _noteActive arrays:
 *   0=APU Pulse1, 1=APU Pulse2, 2=Tri, 3=Noise,
 *   4+ = expansion channels (mapper dependent) */
#ifdef USE_VRC6
#define CH_COUNT 8                     /* 5 APU+DMC + 3 VRC6 */
#elif defined(USE_VRC7)
#define CH_COUNT (5 + VRC7_CH_COUNT)   /* 5 APU+DMC + 6 VRC7 = 11 */
#elif defined(USE_S5B)
#define CH_COUNT (5 + S5B_CH_COUNT)    /* 5 APU+DMC + 3 S5B = 8 */
#elif defined(USE_N163)
#define CH_COUNT (5 + N163_NUM_CHANNELS) /* 5 APU+DMC + 4 N163 = 9 */
#else
#define CH_COUNT 5                     /* APU + DMC */
#endif

static u8 _lastNotes[CH_COUNT];
static u8 _noteActive[CH_COUNT];

/* APU pulse state */
static u8 _duty[2];
static u8 _volume[2];
static u8 _sweepDir[2];
static u8 _sweepShift[2];
static u8 _prevTimerHi[2];   /* track HI byte to avoid phase-reset clicks */

/* APU noise state */
static u8 _noiseVol;

/* ===== DMC state ===== */

typedef struct {
	u8 addr_reg;
	u8 len_reg;
	u8 rate;
	u8 flags;
} DmcDirEntry;

#define DMC_MAX_SAMPLES 64

static DmcDirEntry _dmcDir[DMC_MAX_SAMPLES];
static u8  _dmcCount;
static u8  _dmcBank;
static u8  _dmcRate;       /* 0xFF = use sample default */
static u8  _dmcLoop;
static u8  _dmcAddrOvr;
static u8  _dmcAddrVal;
static u8  _dmcLenVal;
static u8  _dmcPcmMod;
static u8  _dmcPcmVal;
static u8  _dmcLastNote;

/* ===== APU register helpers ===== */

#define PULSE_VOL(ch) ((_duty[ch] << 6) | 0x30 | _volume[ch])

#define PULSE_SWEEP(ch) \
	((_sweepShift[ch] == 0 || _sweepDir[ch] == 0) ? 0x08 : \
	 (0x80 | ((_sweepDir[ch] == 1) ? 0x08 : 0x00) | _sweepShift[ch]))

/* ===== APU channel note-on / note-off ===== */

static void pulse1_note_on(u8 note, u8 vel)
{
	u16 t;
	u8 hi;
	if (note > 127) return;
	t = pulse_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	_volume[0] = vel >> 3;

	if (!_noteActive[0]) {
		_noteActive[0] = 1;
		APU_SQ1_VOL   = PULSE_VOL(0);
		APU_SQ1_SWEEP = PULSE_SWEEP(0);
		APU_SQ1_LO    = (u8)(t);
		hi = (u8)(t >> 8);
		APU_SQ1_HI    = hi;
		_prevTimerHi[0] = hi;
	} else {
		APU_SQ1_VOL = PULSE_VOL(0);
		APU_SQ1_LO  = (u8)(t);
		hi = (u8)(t >> 8);
		if (hi != _prevTimerHi[0]) {
			APU_SQ1_HI = hi;
			_prevTimerHi[0] = hi;
		}
	}
}

static void pulse1_note_off(void)
{
	_noteActive[0] = 0;
	APU_SQ1_VOL = (_duty[0] << 6) | 0x30;
}

static void pulse2_note_on(u8 note, u8 vel)
{
	u16 t;
	u8 hi;
	if (note > 127) return;
	t = pulse_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	_volume[1] = vel >> 3;

	if (!_noteActive[1]) {
		_noteActive[1] = 1;
		APU_SQ2_VOL   = PULSE_VOL(1);
		APU_SQ2_SWEEP = PULSE_SWEEP(1);
		APU_SQ2_LO    = (u8)(t);
		hi = (u8)(t >> 8);
		APU_SQ2_HI    = hi;
		_prevTimerHi[1] = hi;
	} else {
		APU_SQ2_VOL = PULSE_VOL(1);
		APU_SQ2_LO  = (u8)(t);
		hi = (u8)(t >> 8);
		if (hi != _prevTimerHi[1]) {
			APU_SQ2_HI = hi;
			_prevTimerHi[1] = hi;
		}
	}
}

static void pulse2_note_off(void)
{
	_noteActive[1] = 0;
	APU_SQ2_VOL = (_duty[1] << 6) | 0x30;
}

static void tri_note_on(u8 note)
{
	u16 t;
	if (note > 127) return;
	t = tri_timer_lut[note];
	if (t == 0) return;
	if (g_is_pal) t = PAL_SCALE_TIMER(t);

	_noteActive[2] = 1;

	APU_TRI_LINEAR = 0xFF;
	APU_TRI_LO     = (u8)(t);
	APU_TRI_HI     = (u8)(t >> 8);
}

static void tri_note_off(void)
{
	_noteActive[2] = 0;
	APU_TRI_LINEAR = 0x80;
}

static void noise_note_on(u8 note, u8 vel)
{
	u8 idx;

	if (note < NOISE_BASE_NOTE) return;
	idx = note - NOISE_BASE_NOTE;
	if (idx >= NOISE_NOTE_COUNT) return;

	_noiseVol = vel >> 3;

	APU_NOISE_VOL = 0x30 | _noiseVol;
	APU_NOISE_LO  = noise_lut[idx];
	if (!_noteActive[3]) APU_NOISE_HI = 0x08;
	_noteActive[3] = 1;
}

static void noise_note_off(void)
{
	_noteActive[3] = 0;
	APU_NOISE_VOL  = 0x30;
}

/* ===== DMC sample loading ===== */

/*
 * Load a .DMC bank file from SD card.
 *
 * File layout (produced by dmc-pack):
 *   [0-3]   "NDMC" magic
 *   [4]     version
 *   [5]     sample count
 *   [6-7]   reserved
 *   [8...]  directory (count * 4 bytes)
 *   [...]   16384 bytes of DPCM data
 *
 * The DPCM block is DMA'd to the PRG RAM region backing CPU $C000-$FFFF.
 * We save and restore the 6-byte interrupt vector area ($FFFA-$FFFF)
 * around the DMA so NMI/RESET/IRQ vectors are not clobbered.
 */
static u8 dmc_load_bank_file(u8 *path)
{
	u8 resp;
	u8 header[8];
	u8 vectors[DMC_VEC_SIZE];

	gConsPrint("Opening DMC bank...");
	resp = ed_cmd_file_open(path, FA_READ);
	if (resp) return resp;

	/* read & validate header */
	resp = ed_cmd_file_read(header, 8);
	if (resp) { ed_cmd_file_close(); return resp; }

	if (header[0] != 'N' || header[1] != 'D' ||
	    header[2] != 'M' || header[3] != 'C') {
		ed_cmd_file_close();
		return 0xFF;
	}

	_dmcCount = header[5];
	if (_dmcCount > DMC_MAX_SAMPLES) _dmcCount = DMC_MAX_SAMPLES;

	/* read sample directory into CPU RAM */
	resp = ed_cmd_file_read(_dmcDir, (u16)_dmcCount * 4);
	if (resp) { ed_cmd_file_close(); return resp; }

	/* save interrupt vectors before DMA clobbers them */
	ed_cmd_mem_rd(DMC_PRG_ADDR + DMC_BANK_SIZE - DMC_VEC_SIZE,
	              vectors, DMC_VEC_SIZE);

	/* DMA 16KB of DPCM data to PRG RAM ($C000-$FFFF) */
	resp = ed_cmd_file_read_mem(DMC_PRG_ADDR, DMC_BANK_SIZE);
	if (resp) { ed_cmd_file_close(); return resp; }

	/* restore interrupt vectors */
	ed_cmd_mem_wr(DMC_PRG_ADDR + DMC_BANK_SIZE - DMC_VEC_SIZE,
	              vectors, DMC_VEC_SIZE);

	ed_cmd_file_close();
	return 0;
}

static u8 dmc_load_bank(u8 bank)
{
	u8 path[16];
	u8 resp;

	/* "MIDI/BANK01.DMC" or "MIDI/BANK02.DMC" */
	path[0]  = 'M'; path[1]  = 'I'; path[2]  = 'D'; path[3]  = 'I';
	path[4]  = '/';
	path[5]  = 'B'; path[6]  = 'A'; path[7]  = 'N'; path[8]  = 'K';
	path[9]  = '0'; path[10] = '1' + bank;
	path[11] = '.'; path[12] = 'D'; path[13] = 'M'; path[14] = 'C';
	path[15] = 0;

	resp = dmc_load_bank_file(path);
	gConsPrint("bank resp ");
	gAppendNum(resp);
	gRepaint();
	if (resp == 0) _dmcBank = bank;
	return resp;
}

/* ===== DMC playback ===== */

static void dmc_note_on(u8 note, u8 vel)
{
	u8 rate_idx, addr_reg, len_reg;

	if (note >= _dmcCount) return;

	_dmcLastNote   = note;
	_noteActive[4] = 1;

	/* address & length: from directory, or override */
	if (_dmcAddrOvr) {
		addr_reg = _dmcAddrVal;
		len_reg  = _dmcLenVal;
	} else {
		addr_reg = _dmcDir[note].addr_reg;
		len_reg  = _dmcDir[note].len_reg;
	}

	/* rate: CC override, or per-sample default */
	rate_idx = (_dmcRate != 0xFF) ? _dmcRate : _dmcDir[note].rate;
	if (rate_idx > 15) rate_idx = 15;

	/* stop any playing sample (clear DMC enable bit) */
	APU_STATUS = 0x0F;

	/* set initial DAC level from velocity (7-bit) */
	APU_DMC_DAC = vel;

	/* PCM modulation overrides DAC if enabled */
	if (_dmcPcmMod) APU_DMC_DAC = _dmcPcmVal;

	/* configure and start */
	APU_DMC_CTRL = (_dmcLoop ? 0x40 : 0x00) | rate_idx;
	APU_DMC_ADDR = addr_reg;
	APU_DMC_LEN  = len_reg;

	/* enable DMC + all other channels */
	APU_STATUS = 0x1F;
}

static void dmc_note_off(void)
{
	_noteActive[4] = 0;
	if (_dmcLoop) {
		/* stop looping sample; keep other channels */
		APU_STATUS = 0x0F;
	}
	/* one-shot samples just play to completion */
}

/* ===== CC handlers ===== */

static void handle_cc_pulse(u8 ch, u8 cc, u8 val)
{
	switch (cc) {
	case CC_MOD_WHEEL:
		_duty[ch] = val >> 5;
		if (_noteActive[ch]) {
			if (ch == 0) APU_SQ1_VOL = PULSE_VOL(0);
			else         APU_SQ2_VOL = PULSE_VOL(1);
		}
		break;

	case CC_VOLUME:
		_volume[ch] = val >> 3;
		if (_noteActive[ch]) {
			if (ch == 0) APU_SQ1_VOL = PULSE_VOL(0);
			else         APU_SQ2_VOL = PULSE_VOL(1);
		}
		break;

	case CC_SWEEP_DIR:
		if (val < 43)       _sweepDir[ch] = 0;
		else if (val < 86)  _sweepDir[ch] = 1;
		else                _sweepDir[ch] = 2;
		if (_noteActive[ch]) {
			if (ch == 0) APU_SQ1_SWEEP = PULSE_SWEEP(0);
			else         APU_SQ2_SWEEP = PULSE_SWEEP(1);
		}
		break;

	case CC_SWEEP_SHIFT:
		_sweepShift[ch] = (val == 0) ? 0 : ((val >> 4) + 1);
		if (_sweepShift[ch] > 7) _sweepShift[ch] = 7;
		if (_noteActive[ch]) {
			if (ch == 0) APU_SQ1_SWEEP = PULSE_SWEEP(0);
			else         APU_SQ2_SWEEP = PULSE_SWEEP(1);
		}
		break;

	case CC_ALL_NOTES_OFF:
		if (ch == 0) pulse1_note_off();
		else         pulse2_note_off();
		break;
	}
}

static void handle_cc_triangle(u8 cc)
{
	if (cc == CC_ALL_NOTES_OFF) {
		tri_note_off();
	}
}

static void handle_cc_noise(u8 cc, u8 val)
{
	switch (cc) {
	case CC_VOLUME:
		_noiseVol = val >> 3;
		if (_noteActive[3]) {
			APU_NOISE_VOL = 0x30 | _noiseVol;
		}
		break;

	case CC_ALL_NOTES_OFF:
		noise_note_off();
		break;
	}
}

static void handle_cc_dmc(u8 cc, u8 val)
{
	u8 r;

	switch (cc) {

	case CC_DMC_PCM_VAL:
		_dmcPcmVal = val;
		if (_dmcPcmMod) APU_DMC_DAC = val;
		break;

	case CC_DMC_RATE:
		/* CC 0 = reset to per-sample default; 1-127 = rate 0-15 */
		if (val == 0) {
			_dmcRate = 0xFF;
		} else {
			_dmcRate = val >> 3;
			if (_dmcRate > 15) _dmcRate = 15;
		}
		if (_noteActive[4]) {
			r = (_dmcRate != 0xFF) ? _dmcRate : _dmcDir[_dmcLastNote].rate;
			APU_DMC_CTRL = (_dmcLoop ? 0x40 : 0x00) | r;
		}
		break;

	case CC_DMC_LOOP:
		_dmcLoop = (val >= 64) ? 1 : 0;
		if (_noteActive[4]) {
			r = (_dmcRate != 0xFF) ? _dmcRate : _dmcDir[_dmcLastNote].rate;
			APU_DMC_CTRL = (_dmcLoop ? 0x40 : 0x00) | r;
		}
		break;

	case CC_DMC_ADDR_OFS:
		_dmcAddrVal = val;
		/* MidiNES behavior: audition on tweak (retrigger while override active) */
		if (_dmcAddrOvr && _noteActive[4]) {
			APU_STATUS   = 0x0F;
			APU_DMC_ADDR = _dmcAddrVal;
			APU_DMC_LEN  = _dmcLenVal;
			APU_STATUS   = 0x1F;
		}
		break;

	case CC_DMC_LEN_OVR:
		_dmcLenVal = val;
		if (_dmcAddrOvr && _noteActive[4]) {
			APU_STATUS   = 0x0F;
			APU_DMC_ADDR = _dmcAddrVal;
			APU_DMC_LEN  = _dmcLenVal;
			APU_STATUS   = 0x1F;
		}
		break;

	case CC_DMC_ADDR_EN:
		_dmcAddrOvr = (val >= 64) ? 1 : 0;
		break;

	case CC_DMC_PCM_EN:
		_dmcPcmMod = (val >= 64) ? 1 : 0;
		if (_dmcPcmMod) APU_DMC_DAC = _dmcPcmVal;
		break;

	case CC_DMC_BANK:
		{
			u8 newBank = (val >= 64) ? 1 : 0;
			if (newBank != _dmcBank) {
				APU_STATUS = 0x0F;
				_noteActive[4] = 0;

				gConsPrint("Loading bank ");
				gAppendNum((u32)(newBank + 1));
				gAppendString("...");
				gRepaint();

				if (dmc_load_bank(newBank)) {
					gConsPrint("Bank load failed!");
					gRepaint();
				}
			}
		}
		break;

	case CC_ALL_NOTES_OFF:
		dmc_note_off();
		break;
	}
}

/* ===== FIFO helpers ===== */

static void fifo_read_byte(u8 *dst)
{
	while (ed_fifo_busy()) {
	}
	ed_fifo_rd(dst, 1);
}

/* ===== Mapping from MIDI channel to internal state index ===== */

static u8 ch_to_state(u8 channel)
{
	switch (channel) {
	case MIDI_CH_PULSE1:   return 0;
	case MIDI_CH_PULSE2:   return 1;
	case MIDI_CH_TRIANGLE: return 2;
	case MIDI_CH_NOISE:    return 3;
	case MIDI_CH_DMC:      return 4;
#ifdef USE_VRC6
	case MIDI_CH_VRC6_P1:  return 5;
	case MIDI_CH_VRC6_P2:  return 6;
	case MIDI_CH_VRC6_SAW: return 7;
#elif defined(USE_VRC7)
	case MIDI_CH_VRC7_0:   return 5;
	case MIDI_CH_VRC7_1:   return 6;
	case MIDI_CH_VRC7_2:   return 7;
	case MIDI_CH_VRC7_3:   return 8;
	case MIDI_CH_VRC7_4:   return 9;
	case MIDI_CH_VRC7_5:   return 10;
#elif defined(USE_S5B)
	case MIDI_CH_S5B_A:    return 5;
	case MIDI_CH_S5B_B:    return 6;
	case MIDI_CH_S5B_C:    return 7;
#elif defined(USE_N163)
	case MIDI_CH_N163_0:   return 5;
	case MIDI_CH_N163_1:   return 6;
	case MIDI_CH_N163_2:   return 7;
	case MIDI_CH_N163_3:   return 8;
#endif
	default:               return 0xFF;
	}
}

/* ===== Main ===== */

static const char * const tv_names[3] = {
	"EverDrive-N8 MIDI [NTSC]",
	"EverDrive-N8 MIDI [PAL]",
	"EverDrive-N8 MIDI [Dendy]"
};

void main(void)
{
	u8 i, tv, resp;

	sysInit();
	ed_init();

	tv = getTVSystem();
	if (tv > 2) tv = 0;
	g_is_pal = (tv != 0);

	/* Enable APU channels; inhibit frame counter IRQ */
	APU_STATUS = 0x0F;
	APU_FRAME  = 0x40;

	/* Init APU state */
	for (i = 0; i < CH_COUNT; i++) {
		_lastNotes[i]  = 0;
		_noteActive[i] = 0;
	}
	_duty[0] = 2; _duty[1] = 2;
	_volume[0] = 15; _volume[1] = 15;
	_sweepDir[0] = 0; _sweepDir[1] = 0;
	_sweepShift[0] = 0; _sweepShift[1] = 0;
	_prevTimerHi[0] = 0xFF; _prevTimerHi[1] = 0xFF;
	_noiseVol = 15;

	/* Init DMC state */
	_dmcCount    = 0;
	_dmcBank     = 0;
	_dmcRate     = 0xFF;
	_dmcLoop     = 0;
	_dmcAddrOvr  = 0;
	_dmcAddrVal  = 0;
	_dmcLenVal   = 0;
	_dmcPcmMod   = 0;
	_dmcPcmVal   = 0;
	_dmcLastNote = 0;

	APU_SQ1_SWEEP = 0x08;
	APU_SQ2_SWEEP = 0x08;

#ifdef USE_VRC6
	vrc6_init();
#elif defined(USE_VRC7)
	vrc7_init();
#elif defined(USE_S5B)
	s5b_init();
#elif defined(USE_N163)
	n163_init();
#endif

	gClearScreen();
	gConsPrint("");
	gConsPrint("");
	gConsPrintCX((u8 *)tv_names[tv]);
	gConsPrint("");
	gConsPrint("ch1=Pulse1 ch2=Pulse2");
	gConsPrint("ch3=Tri    ch4=Noise");
	gConsPrint("ch5=DMC");
#ifdef USE_VRC6
	gConsPrint("ch6=VRC6P1 ch7=VRC6P2");
	gConsPrint("ch8=VRC6Saw");
#elif defined(USE_VRC7)
	gConsPrint("ch6-11=VRC7 FM 1-6");
#elif defined(USE_S5B)
	gConsPrint("ch6=S5B-A  ch7=S5B-B");
	gConsPrint("ch8=S5B-C");
#elif defined(USE_N163)
	gConsPrint("ch6=N163-0 ch7=N163-1");
	gConsPrint("ch8=N163-2 ch9=N163-3");
#endif
	gConsPrint("");
	gConsPrint("CC1=Duty CC7=Vol");
	gConsPrint("CC75=SweepDir CC76=Shift");
	gConsPrint("");

	/* init SD and load default sample bank */
	gConsPrint("Init SD...");
	gRepaint();

	resp = 0;
	if (resp) {
		gConsPrint("SD init failed!");
		gRepaint();
	} else {
		gConsPrint("Loading bank 1...");
		gRepaint();

		resp = dmc_load_bank(0);
		if (resp) {
			gConsPrint("MIDI/BANK01.DMC not found");
			gRepaint();
		} else {
			gConsPrint("Loaded ");
			gAppendNum((u32)_dmcCount);
			gAppendString(" samples");
			gRepaint();
		}
	}

	gConsPrint("");
	gConsPrint("Ready!");
	gRepaint();

	midiRead();

	while (1);
}

/* ===== MIDI parser ===== */

/* #define LOG_MIDI */

void midiRead(void)
{
	u8 status, channel, note, vel;
	u8 cc, ccval;
	u8 stateIdx;

	while (1) {
		do {
			fifo_read_byte(&status);
		} while (!(status & 0x80));

		channel = status & 0x0F;
		stateIdx = ch_to_state(channel);

		if ((status & 0xF0) == 0x90) {          /* Note On */
			fifo_read_byte(&note);
			fifo_read_byte(&vel);

			if (stateIdx == 0xFF) continue;

			if (vel > 0) {
				switch (channel) {
				case MIDI_CH_PULSE1:   pulse1_note_on(note, vel);    break;
				case MIDI_CH_PULSE2:   pulse2_note_on(note, vel);    break;
				case MIDI_CH_TRIANGLE: tri_note_on(note);            break;
				case MIDI_CH_NOISE:    noise_note_on(note, vel);     break;
				case MIDI_CH_DMC:      dmc_note_on(note, vel);       break;
#ifdef USE_VRC7
				case MIDI_CH_VRC7_0:   vrc7_note_on(0, note, vel);  break;
				case MIDI_CH_VRC7_1:   vrc7_note_on(1, note, vel);  break;
				case MIDI_CH_VRC7_2:   vrc7_note_on(2, note, vel);  break;
				case MIDI_CH_VRC7_3:   vrc7_note_on(3, note, vel);  break;
				case MIDI_CH_VRC7_4:   vrc7_note_on(4, note, vel);  break;
				case MIDI_CH_VRC7_5:   vrc7_note_on(5, note, vel);  break;
#elif defined(USE_S5B)
				case MIDI_CH_S5B_A:    s5b_note_on(0, note, vel);   break;
				case MIDI_CH_S5B_B:    s5b_note_on(1, note, vel);   break;
				case MIDI_CH_S5B_C:    s5b_note_on(2, note, vel);   break;
#elif defined(USE_N163)
				case MIDI_CH_N163_0:   n163_note_on(0, note, vel);  break;
				case MIDI_CH_N163_1:   n163_note_on(1, note, vel);  break;
				case MIDI_CH_N163_2:   n163_note_on(2, note, vel);  break;
				case MIDI_CH_N163_3:   n163_note_on(3, note, vel);  break;
#elif defined(USE_VRC6)
				case MIDI_CH_VRC6_P1:  vrc6_p1_note_on(note, vel);   break;
				case MIDI_CH_VRC6_P2:  vrc6_p2_note_on(note, vel);   break;
				case MIDI_CH_VRC6_SAW: vrc6_saw_note_on(note, vel);  break;
#endif
				}
				_lastNotes[stateIdx] = note;
#ifdef LOG_MIDI
				gConsPrint("On  ch");
				gAppendNum((u32)(channel + 1));
				gAppendString(" n=");
				gAppendNum((u32)note);
				gAppendString(" v=");
				gAppendNum((u32)vel);
				gRepaint();
#endif
			} else {                            /* vel=0 is Note Off */
				if (_lastNotes[stateIdx] != note) continue;
				switch (channel) {
				case MIDI_CH_PULSE1:   pulse1_note_off();    break;
				case MIDI_CH_PULSE2:   pulse2_note_off();    break;
				case MIDI_CH_TRIANGLE: tri_note_off();       break;
				case MIDI_CH_NOISE:    noise_note_off();     break;
				case MIDI_CH_DMC:      dmc_note_off();       break;
#ifdef USE_VRC7
				case MIDI_CH_VRC7_0:   vrc7_note_off(0);    break;
				case MIDI_CH_VRC7_1:   vrc7_note_off(1);    break;
				case MIDI_CH_VRC7_2:   vrc7_note_off(2);    break;
				case MIDI_CH_VRC7_3:   vrc7_note_off(3);    break;
				case MIDI_CH_VRC7_4:   vrc7_note_off(4);    break;
				case MIDI_CH_VRC7_5:   vrc7_note_off(5);    break;
#elif defined(USE_S5B)
				case MIDI_CH_S5B_A:    s5b_note_off(0);     break;
				case MIDI_CH_S5B_B:    s5b_note_off(1);     break;
				case MIDI_CH_S5B_C:    s5b_note_off(2);     break;
#elif defined(USE_N163)
				case MIDI_CH_N163_0:   n163_note_off(0);    break;
				case MIDI_CH_N163_1:   n163_note_off(1);    break;
				case MIDI_CH_N163_2:   n163_note_off(2);    break;
				case MIDI_CH_N163_3:   n163_note_off(3);    break;
#elif defined(USE_VRC6)
				case MIDI_CH_VRC6_P1:  vrc6_p1_note_off();   break;
				case MIDI_CH_VRC6_P2:  vrc6_p2_note_off();   break;
				case MIDI_CH_VRC6_SAW: vrc6_saw_note_off();  break;
#endif
				}
#ifdef LOG_MIDI
				gConsPrint("Off ch");
				gAppendNum((u32)(channel + 1));
				gAppendString(" n=");
				gAppendNum((u32)note);
				gRepaint();
#endif
			}

		} else if ((status & 0xF0) == 0x80) {   /* Note Off */
			fifo_read_byte(&note);
			fifo_read_byte(&vel);

			if (stateIdx == 0xFF) continue;
			if (_lastNotes[stateIdx] != note) continue;

			switch (channel) {
			case MIDI_CH_PULSE1:   pulse1_note_off();    break;
			case MIDI_CH_PULSE2:   pulse2_note_off();    break;
			case MIDI_CH_TRIANGLE: tri_note_off();       break;
			case MIDI_CH_NOISE:    noise_note_off();     break;
			case MIDI_CH_DMC:      dmc_note_off();       break;
#ifdef USE_VRC7
			case MIDI_CH_VRC7_0:   vrc7_note_off(0);    break;
			case MIDI_CH_VRC7_1:   vrc7_note_off(1);    break;
			case MIDI_CH_VRC7_2:   vrc7_note_off(2);    break;
			case MIDI_CH_VRC7_3:   vrc7_note_off(3);    break;
			case MIDI_CH_VRC7_4:   vrc7_note_off(4);    break;
			case MIDI_CH_VRC7_5:   vrc7_note_off(5);    break;
#elif defined(USE_S5B)
			case MIDI_CH_S5B_A:    s5b_note_off(0);     break;
			case MIDI_CH_S5B_B:    s5b_note_off(1);     break;
			case MIDI_CH_S5B_C:    s5b_note_off(2);     break;
#elif defined(USE_N163)
			case MIDI_CH_N163_0:   n163_note_off(0);    break;
			case MIDI_CH_N163_1:   n163_note_off(1);    break;
			case MIDI_CH_N163_2:   n163_note_off(2);    break;
			case MIDI_CH_N163_3:   n163_note_off(3);    break;
#elif defined(USE_VRC6)
			case MIDI_CH_VRC6_P1:  vrc6_p1_note_off();   break;
			case MIDI_CH_VRC6_P2:  vrc6_p2_note_off();   break;
			case MIDI_CH_VRC6_SAW: vrc6_saw_note_off();  break;
#endif
			}
#ifdef LOG_MIDI
			gConsPrint("Off ch");
			gAppendNum((u32)(channel + 1));
			gAppendString(" n=");
			gAppendNum((u32)note);
			gRepaint();
#endif

		} else if ((status & 0xF0) == 0xB0) {   /* Control Change */
			fifo_read_byte(&cc);
			fifo_read_byte(&ccval);

			switch (channel) {
			case MIDI_CH_PULSE1:   handle_cc_pulse(0, cc, ccval);       break;
			case MIDI_CH_PULSE2:   handle_cc_pulse(1, cc, ccval);       break;
			case MIDI_CH_TRIANGLE: handle_cc_triangle(cc);              break;
			case MIDI_CH_NOISE:    handle_cc_noise(cc, ccval);          break;
			case MIDI_CH_DMC:      handle_cc_dmc(cc, ccval);            break;
#ifdef USE_VRC7
			case MIDI_CH_VRC7_0:   handle_cc_vrc7(0, cc, ccval);       break;
			case MIDI_CH_VRC7_1:   handle_cc_vrc7(1, cc, ccval);       break;
			case MIDI_CH_VRC7_2:   handle_cc_vrc7(2, cc, ccval);       break;
			case MIDI_CH_VRC7_3:   handle_cc_vrc7(3, cc, ccval);       break;
			case MIDI_CH_VRC7_4:   handle_cc_vrc7(4, cc, ccval);       break;
			case MIDI_CH_VRC7_5:   handle_cc_vrc7(5, cc, ccval);       break;
#elif defined(USE_S5B)
			case MIDI_CH_S5B_A:    handle_cc_s5b(0, cc, ccval);        break;
			case MIDI_CH_S5B_B:    handle_cc_s5b(1, cc, ccval);        break;
			case MIDI_CH_S5B_C:    handle_cc_s5b(2, cc, ccval);        break;
#elif defined(USE_N163)
			case MIDI_CH_N163_0:   handle_cc_n163(0, cc, ccval);       break;
			case MIDI_CH_N163_1:   handle_cc_n163(1, cc, ccval);       break;
			case MIDI_CH_N163_2:   handle_cc_n163(2, cc, ccval);       break;
			case MIDI_CH_N163_3:   handle_cc_n163(3, cc, ccval);       break;
#elif defined(USE_VRC6)
			case MIDI_CH_VRC6_P1:  handle_cc_vrc6_pulse(0, cc, ccval);  break;
			case MIDI_CH_VRC6_P2:  handle_cc_vrc6_pulse(1, cc, ccval);  break;
			case MIDI_CH_VRC6_SAW: handle_cc_vrc6_saw(cc, ccval);       break;
#endif
			}
#ifdef LOG_MIDI
			gConsPrint("CC  ch");
			gAppendNum((u32)(channel + 1));
			gAppendString(" cc");
			gAppendNum((u32)cc);
			gAppendString("=");
			gAppendNum((u32)ccval);
			gRepaint();
#endif

		} else if ((status & 0xF0) == 0xE0 ||
		           (status & 0xF0) == 0xA0) {    /* Pitch Bend / Poly AT */
			fifo_read_byte(&note);               /* drain 2 data bytes */
			fifo_read_byte(&vel);

		} else if ((status & 0xF0) == 0xC0 ||
		           (status & 0xF0) == 0xD0) {    /* Program Change / Ch AT */
			fifo_read_byte(&note);               /* drain 1 data byte */
		}
	}
}