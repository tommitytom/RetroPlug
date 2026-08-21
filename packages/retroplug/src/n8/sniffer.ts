// Decodes the Everdrive N8 Pro's live save-state "sniffer" region into a structured snapshot of a RUNNING
// NES game's hardware state. The N8's FPGA continuously mirrors every CPU write to the PPU/APU/OAM/mapper
// into a device-memory region at Edio ADDR_SSR (0x1802000); a plain Edio.memRD(ADDR_SSR, 0x200) reads it -
// no trigger, no config (edn8-pro-pub fpga/base_sv/sst.sv, `SST_ON` by default; the mirror is disabled only
// in the menu's map-255 core, so a read at the file browser is all zeros). Pure + host-agnostic like
// menuImage.ts (this is decoding, not protocol - there is no C++ twin and no native consumer).
//
// RAW offsets within the region (base ADDR_SSR; = the OS save-file offset minus 0x1800, per everdrive.h +
// sst.sv): mapper regs +0x000 (128 B), APU $4000-$401F write-mirror +0x080 (32 B), PPU palette $3F00-$3F1F
// +0x0A0 (32 B), PPU ctrl/mask/scroll +0x0C0 (4 B), magic 'S' (0x53) +0x0CF, OAM +0x100 (256 B).
//
// SCOPE: only the write-snoop mirror is here. WRAM, VRAM/CHR, and the CPU registers (a/x/y/sp/pc) are NOT -
// the +0x0C8 CPU slot reads 0xFF; those live only in the OS-assembled save file after a *triggered*
// save-state, a separate capability.

const OFF_MAPPER = 0x000; // 128 B mapper registers
const OFF_APU = 0x080; // 32 B - $4000-$401F write mirror (reg $40xx -> region[OFF_APU + (xx & 0x1f)])
const OFF_PPU_PAL = 0x0a0; // 32 B - $3F00-$3F1F palette
const OFF_PPU_REGS = 0x0c0; // 4 B - ctrl($2000), mask($2001), scroll-x, scroll-y ($2005 latches)
const OFF_MAGIC = 0x0cf; // 1 B - always 0x53 ('S') while a game core is running
const OFF_OAM = 0x100; // 256 B - sprite RAM

export const SNIFFER_REGION_SIZE = 0x200; // one memRD covers mapper..OAM
export const SNIFFER_MAGIC = 0x53; // 'S'
export const CPU_HZ_NTSC = 1789773; // 2A03 NTSC; PAL 2A07 is 1662607

/** One of the two 2A03 square channels ($4000-$4003 / $4004-$4007). */
export interface ApuPulse {
  enabled: boolean; // $4015 enable bit for this channel
  duty: number; // 0-3 (12.5% / 25% / 50% / 75%)
  volume: number; // 0-15 (constant volume, or envelope period when constVol is false)
  constVol: boolean; // $400x bit 4: constant volume vs envelope
  lengthHalt: boolean; // $400x bit 5: length-counter halt / envelope loop
  timer: number; // 11-bit period register
  frequency: number; // Hz; 0 when timer < 8 (channel muted by the sweep unit)
}

/** The triangle channel ($4008-$400B). */
export interface ApuTriangle {
  enabled: boolean;
  control: boolean; // $4008 bit 7: length-counter halt / linear-counter control
  timer: number; // 11-bit period register
  frequency: number; // Hz; 0 when timer is 0
}

/** The noise channel ($400C-$400F). */
export interface ApuNoise {
  enabled: boolean;
  volume: number; // 0-15
  constVol: boolean;
  lengthHalt: boolean;
  mode: boolean; // $400E bit 7: short (93-step) mode
  periodIndex: number; // 0-15 into the NTSC noise period table
}

/** The DMC / sample channel ($4010-$4013). */
export interface ApuDmc {
  enabled: boolean; // $4015 bit 4
  irqEnabled: boolean; // $4010 bit 7
  loop: boolean; // $4010 bit 6
  rateIndex: number; // $4010 bits 0-3
  level: number; // $4011 bits 0-6 direct load
}

export interface SnifferApu {
  pulse1: ApuPulse;
  pulse2: ApuPulse;
  triangle: ApuTriangle;
  noise: ApuNoise;
  dmc: ApuDmc;
  enableReg: number; // raw $4015
  frameMode5Step: boolean; // $4017 bit 7
  frameIrqInhibit: boolean; // $4017 bit 6
}

export interface SnifferPpu {
  ctrl: number; // $2000
  mask: number; // $2001
  scrollX: number; // first $2005 write
  scrollY: number; // second $2005 write
  showBackground: boolean; // mask bit 3
  showSprites: boolean; // mask bit 4
}

export interface SnifferSprite {
  index: number; // 0-63
  y: number; // top of sprite (screen y = this + 1)
  tile: number;
  attr: number;
  x: number;
}

export interface SnifferSnapshot {
  magicOk: boolean; // region[+0xCF] === 0x53: a game core is running and the mirror is live
  apu: SnifferApu;
  ppu: SnifferPpu;
  palette: Uint8Array; // 32 NES colour indices ($3F00-$3F1F)
  oam: Uint8Array; // 256 raw bytes
  activeSprites: number; // sprites with an on-screen Y (< 0xEF)
  mapperRegs: Uint8Array; // first 16 raw mapper register bytes
}

const pulseFreq = (timer: number, cpuHz: number): number =>
  timer < 8 ? 0 : Math.round(cpuHz / (16 * (timer + 1)));
const triangleFreq = (timer: number, cpuHz: number): number =>
  timer === 0 ? 0 : Math.round(cpuHz / (32 * (timer + 1)));

function decodePulse(r: Uint8Array, base: number, enabled: boolean, cpuHz: number): ApuPulse {
  const ctrl = r[base];
  const timer = ((r[base + 3] & 0x07) << 8) | r[base + 2];
  return {
    enabled,
    duty: (ctrl >> 6) & 0x03,
    volume: ctrl & 0x0f,
    constVol: (ctrl & 0x10) !== 0,
    lengthHalt: (ctrl & 0x20) !== 0,
    timer,
    frequency: pulseFreq(timer, cpuHz),
  };
}

/** Decode a 512-byte Edio.memRD(ADDR_SSR, SNIFFER_REGION_SIZE) into a running game's live state. `cpuHz`
 *  defaults to NTSC; pass CPU_HZ_PAL-equivalent for a PAL console. `magicOk` is false when no game core is
 *  running (e.g. read at the menu) - the caller should treat the rest as meaningless in that case. */
export function decodeSniffer(region: Uint8Array, cpuHz: number = CPU_HZ_NTSC): SnifferSnapshot {
  if (region.length < SNIFFER_REGION_SIZE)
    throw new Error(`sniffer region too small: ${region.length} < ${SNIFFER_REGION_SIZE}`);

  const magicOk = region[OFF_MAGIC] === SNIFFER_MAGIC;
  const apu = region.subarray(OFF_APU, OFF_APU + 0x20); // $4000-$401F
  const enableReg = apu[0x15];
  const frame = apu[0x17];

  const noiseCtrl = apu[0x0c];
  const noiseHi = apu[0x0e];
  const dmcCtrl = apu[0x10];

  const oam = region.slice(OFF_OAM, OFF_OAM + 0x100);
  let activeSprites = 0;
  for (let i = 0; i < 64; i++) if (oam[i * 4] < 0xef) activeSprites++;

  const mask = region[OFF_PPU_REGS + 1];

  return {
    magicOk,
    apu: {
      pulse1: decodePulse(apu, 0x00, (enableReg & 0x01) !== 0, cpuHz),
      pulse2: decodePulse(apu, 0x04, (enableReg & 0x02) !== 0, cpuHz),
      triangle: {
        enabled: (enableReg & 0x04) !== 0,
        control: (apu[0x08] & 0x80) !== 0,
        timer: ((apu[0x0b] & 0x07) << 8) | apu[0x0a],
        frequency: triangleFreq(((apu[0x0b] & 0x07) << 8) | apu[0x0a], cpuHz),
      },
      noise: {
        enabled: (enableReg & 0x08) !== 0,
        volume: noiseCtrl & 0x0f,
        constVol: (noiseCtrl & 0x10) !== 0,
        lengthHalt: (noiseCtrl & 0x20) !== 0,
        mode: (noiseHi & 0x80) !== 0,
        periodIndex: noiseHi & 0x0f,
      },
      dmc: {
        enabled: (enableReg & 0x10) !== 0,
        irqEnabled: (dmcCtrl & 0x80) !== 0,
        loop: (dmcCtrl & 0x40) !== 0,
        rateIndex: dmcCtrl & 0x0f,
        level: apu[0x11] & 0x7f,
      },
      enableReg,
      frameMode5Step: (frame & 0x80) !== 0,
      frameIrqInhibit: (frame & 0x40) !== 0,
    },
    ppu: {
      ctrl: region[OFF_PPU_REGS],
      mask,
      scrollX: region[OFF_PPU_REGS + 2],
      scrollY: region[OFF_PPU_REGS + 3],
      showBackground: (mask & 0x08) !== 0,
      showSprites: (mask & 0x10) !== 0,
    },
    palette: region.slice(OFF_PPU_PAL, OFF_PPU_PAL + 0x20),
    oam,
    activeSprites,
    mapperRegs: region.slice(OFF_MAPPER, OFF_MAPPER + 0x10),
  };
}

/** The sprite list decoded from OAM (64 entries). Separate from decodeSniffer so a caller pays for it only
 *  when it wants sprites. */
export function decodeSprites(oam: Uint8Array): SnifferSprite[] {
  const out: SnifferSprite[] = [];
  for (let i = 0; i < 64; i++) {
    const o = i * 4;
    out.push({ index: i, y: oam[o], tile: oam[o + 1], attr: oam[o + 2], x: oam[o + 3] });
  }
  return out;
}
