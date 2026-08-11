// F6: decode the raw expansion-audio register writes drainEvents() logs into the final per-voice
// programmed values - the white-box "what did the ROM actually write for this note" that pairs with F1's
// decoded-Hz live read (getExpansionAudioState().frequency). It replays each chip's write protocol in
// order (VRC6 is direct-addressed; VRC7/S5B/N163 are latch+data, N163 with an auto-incrementing RAM
// pointer), reconstructing the register file, then reads back the per-voice fields. Surfaces write-path
// bugs the audio can hide, e.g. an N163 that loads a 128-sample wave-length byte where the freq register
// was computed for 32 (the field pair that let N163 ship an octave off).
//
// drainEvents is frame-scoped (current + previous PPU frame) and the debugger must be warm before the write
// frame, so a caller typically warms up early and polls densely across the note, accumulating events. That
// accumulation double-counts the overlapping previous-frame slice, which would corrupt N163's ordered
// pointer replay - so this dedups by execution position (address,value,pc,scanline,cycle) first, keeping a
// single in-order copy of each real write.

import type { DebugEvent } from "../src/backend";

export type ExpansionChip = "vrc6" | "vrc7" | "s5b" | "n163";

export interface Vrc6VoiceWrite {
  channel: number;         // 0 pulse1, 1 pulse2, 2 saw
  kind: "pulse" | "saw";
  freqReg: number;         // 12-bit timer (the `period` F1 reports)
  enabled: boolean;        // channel enable bit
  volume: number;          // pulse: 0-15 volume; saw: 0-63 accumulator rate
  duty: number;            // pulse duty 0-7 (0 for saw)
  ignoreDuty: boolean;     // pulse "ignore duty" -> DC, no tone (false for saw)
  freqShift: number;       // shared $9003 shift 0/4/8 (right-shifts the timer)
  haltAudio: boolean;      // shared $9003 halt bit
}

export interface Vrc7VoiceWrite {
  channel: number;         // 0..5
  fnum: number;            // 9-bit F-number (the `period` F1 reports)
  block: number;           // 3-bit octave
  key: boolean;            // key-on bit
  inst: number;            // patch 0=custom, 1-15 ROM
  volume: number;          // 0-15 (register is attenuation; this is loud-scale, 15 loudest)
}

export interface S5bVoiceWrite {
  channel: number;         // 0..2
  period: number;          // 12-bit tone period (the `period` F1 reports)
  volume: number;          // 4-bit
  toneEnabled: boolean;    // mixer tone-enable (active-low bit in reg 7)
}

export interface N163VoiceWrite {
  channel: number;         // 0..7 (hardware slot)
  enabled: boolean;        // in the active set (top numChannels slots) and sound not disabled
  freqReg: number;         // 18-bit frequency register (the `period` F1 reports)
  waveLen: number;         // active wave length in samples = 256 - (reg & 0xFC)
  waveAddr: number;        // wave start offset in the 4-bit-sample RAM
  volume: number;          // 4-bit
  numChannels: number;     // enabled voice count 1-8 (chip-global)
}

export interface DecodedExpansionWrites {
  vrc6?: Vrc6VoiceWrite[];
  vrc7?: Vrc7VoiceWrite[];
  s5b?: S5bVoiceWrite[];
  n163?: N163VoiceWrite[];
}

/** Writes (operationType 1, Register events) in execution order, deduped across overlapping poll windows. */
function orderedWrites(events: DebugEvent[]): DebugEvent[] {
  const seen = new Set<string>();
  const out: DebugEvent[] = [];
  for (const e of events) {
    if (e.operationType !== 1) continue; // 1 = Write
    const key = `${e.address}:${e.value}:${e.programCounter}:${e.scanline}:${e.cycle}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(e);
  }
  return out;
}

function decodeVrc6(w: DebugEvent[]): Vrc6VoiceWrite[] {
  // Direct-addressed (Mesen masks addr & 0xF003): $9xxx pulse1, $Axxx pulse2, $Bxxx saw; low 2 bits pick
  // the sub-register. $9003 holds the shared halt + frequency-shift.
  const reg = { 0x9000: [0, 0, 0], 0xa000: [0, 0, 0], 0xb000: [0, 0, 0] } as Record<number, number[]>;
  let shiftHalt = 0;
  for (const e of w) {
    // Mesen routes VRC6 audio via `switch(addr & 0xF003)`, so match on the masked address (mirrors like
    // $9007 also hit $9003's halt/shift; $9004 hits $9000's control).
    if ((e.address & 0xf003) === 0x9003) { shiftHalt = e.value; continue; }
    const base = e.address & 0xf000;
    const sub = e.address & 0x3; // 0..2 (sub==3 is the halt/shift case handled above)
    if ((base === 0x9000 || base === 0xa000 || base === 0xb000) && sub < 3) reg[base][sub] = e.value & 0xff;
  }
  const freqShift = (shiftHalt & 0x04) ? 8 : (shiftHalt & 0x02) ? 4 : 0;
  const haltAudio = (shiftHalt & 0x01) !== 0;
  const pulse = (base: number, channel: number): Vrc6VoiceWrite => {
    const r = reg[base];
    return {
      channel, kind: "pulse",
      freqReg: ((r[2] & 0x0f) << 8) | r[1],
      enabled: (r[2] & 0x80) !== 0,
      volume: r[0] & 0x0f,
      duty: (r[0] & 0x70) >> 4,
      ignoreDuty: (r[0] & 0x80) !== 0,
      freqShift, haltAudio,
    };
  };
  const r = reg[0xb000];
  const saw: Vrc6VoiceWrite = {
    channel: 2, kind: "saw",
    freqReg: ((r[2] & 0x0f) << 8) | r[1],
    enabled: (r[2] & 0x80) !== 0,
    volume: r[0] & 0x3f,   // accumulator rate 0-63
    duty: 0, ignoreDuty: false, freqShift, haltAudio,
  };
  return [pulse(0x9000, 0), pulse(0xa000, 1), saw];
}

function decodeVrc7(w: DebugEvent[]): Vrc7VoiceWrite[] {
  // Latch+data. Mesen routes VRC7 writes via `switch(addr & 0xF038)`: $9010 selects the OPLL register,
  // $9030 writes it, $E000 bit6 mutes (while muted both $9010/$9030 are disregarded, per Vrc7Audio::WriteReg).
  const reg = new Uint8Array(0x40);
  let sel = 0;
  let muted = false;
  for (const e of w) {
    const a = e.address & 0xf038;
    if (a === 0xe000) muted = (e.value & 0x40) !== 0;
    else if (a === 0x9010) { if (!muted) sel = e.value & 0xff; }
    else if (a === 0x9030) { if (!muted && sel < reg.length) reg[sel] = e.value & 0xff; }
  }
  const out: Vrc7VoiceWrite[] = [];
  for (let ch = 0; ch < 6; ch++) {
    const r1 = reg[0x10 + ch], r2 = reg[0x20 + ch], r3 = reg[0x30 + ch];
    out.push({
      channel: ch,
      fnum: r1 | ((r2 & 0x01) << 8),
      block: (r2 >> 1) & 0x07,
      key: (r2 & 0x10) !== 0,
      inst: r3 >> 4,
      volume: 15 - (r3 & 0x0f),
    });
  }
  return out;
}

function decodeS5b(w: DebugEvent[]): S5bVoiceWrite[] {
  // Latch+data (Mesen masks addr & 0xE000): $C000 selects the PSG register, $E000 writes it.
  const reg = new Uint8Array(0x10);
  let sel = 0;
  for (const e of w) {
    const a = e.address & 0xe000;
    // Mesen stores the full 8-bit register-select ($C000) and only writes ($E000) when it is <= 0x0F.
    if (a === 0xc000) sel = e.value & 0xff;
    else if (a === 0xe000) { if (sel <= 0x0f) reg[sel] = e.value & 0xff; }
  }
  const out: S5bVoiceWrite[] = [];
  for (let ch = 0; ch < 3; ch++) {
    out.push({
      channel: ch,
      period: reg[ch * 2] | (reg[ch * 2 + 1] << 8),
      volume: reg[8 + ch] & 0x0f,
      toneEnabled: ((reg[7] >> ch) & 0x01) === 0, // active-low
    });
  }
  return out;
}

function decodeN163(w: DebugEvent[]): N163VoiceWrite[] {
  // Latch+data with an auto-incrementing RAM pointer (Mesen masks addr & 0xF800): $F800 sets the pointer
  // (+ auto-increment flag), $4800 writes the 128-byte internal RAM at the pointer, $E000 bit6 disables
  // sound. Must replay in order; the sound registers live at 0x40 + channel*8.
  //
  // CAVEAT: this order-sensitive replay is best-effort. It is exact for a SINGLE drainEvents() capture (one
  // note-on burst). Across an accumulation of overlapping polls the dedup key is PPU-frame-relative, so a
  // ROM that re-writes N163 every frame can drop a position-colliding pointer set and desync the pointer;
  // and a $4800 READ with auto-increment (not in this write-only log) also advances the real pointer. For a
  // robust N163 pitch/length readout prefer F1: getExpansionAudioState() reads the live internal RAM directly
  // (its `frequency` / `waveLength` / `activeChannels` need no replay).
  const ram = new Uint8Array(0x80);
  let pos = 0;
  let autoInc = false;
  let disableSound = false;
  for (const e of w) {
    const a = e.address & 0xf800;
    if (a === 0xf800) { pos = e.value & 0x7f; autoInc = (e.value & 0x80) !== 0; }
    else if (a === 0x4800) { ram[pos] = e.value & 0xff; if (autoInc) pos = (pos + 1) & 0x7f; }
    else if (a === 0xe000) { disableSound = (e.value & 0x40) !== 0; }
  }
  const numChannels = ((ram[0x7f] >> 4) & 0x07) + 1;
  const firstActive = 8 - numChannels; // active voices occupy the top slots (7 down to 8-numChannels)
  const out: N163VoiceWrite[] = [];
  for (let ch = 0; ch < 8; ch++) {
    const base = 0x40 + ch * 8;
    out.push({
      channel: ch,
      enabled: !disableSound && ch >= firstActive,
      freqReg: ((ram[base + 0x04] & 0x03) << 16) | (ram[base + 0x02] << 8) | ram[base + 0x00],
      waveLen: 256 - (ram[base + 0x04] & 0xfc),
      waveAddr: ram[base + 0x06],
      volume: ram[base + 0x07] & 0x0f,
      numChannels,
    });
  }
  return out;
}

/** Decode a frame's (or an accumulated run's) expansion-audio register writes into the final per-voice
 *  programmed values for `chip`. Events other than that chip's ports are ignored. */
export function decodeExpansionWrites(events: DebugEvent[], chip: ExpansionChip): DecodedExpansionWrites {
  const w = orderedWrites(events);
  switch (chip) {
    case "vrc6": return { vrc6: decodeVrc6(w) };
    case "vrc7": return { vrc7: decodeVrc7(w) };
    case "s5b":  return { s5b: decodeS5b(w) };
    case "n163": return { n163: decodeN163(w) };
  }
}
