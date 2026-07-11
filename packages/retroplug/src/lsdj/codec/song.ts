// Binary codec for the 0x8000-byte LSDj song body — the pure-TS port of
// SongCodec.cpp (a faithful liblsdj port). decode reads the semantic model out of
// the packed bytes; encode writes it back (the exact inverse). Every format-
// version branch (fmt 0..22) is preserved verbatim: the region layout is stable,
// only per-field bit arithmetic differs. Produces/consumes the model.ts shapes
// directly (no zod on this path).
import { BitView, BitWriter } from "./bits";
import * as R from "./regions";
import {
  PanningNames,
  TableModeNames,
  PulseWidthNames,
  VibratoShapeNames,
  VibratoDirectionNames,
  PlvSpeedNames,
  WavePlayModeNames,
  KitLoopModeNames,
  KitDistortionNames,
  NoiseStabilityNames,
  CloneModeNames,
  SynthWaveformNames,
  SynthFilterNames,
  SynthDistortionNames,
  CommandNames,
  SYNC_TO_BYTE,
  SYNC_FROM_BYTE,
  type Song,
  type Instrument,
  type Vibrato,
  type Adsr,
  type Command,
} from "../model";

// Working-song factory default for an UNALLOCATED instrument slot on fmt<=10
// (fmt>=11 zeroes them). Distinct from rle.ts's DEFAULT_INSTRUMENT (same bytes,
// rotated for the compressed stream).
const kDefaultInstrumentOld = [0x00, 0xa8, 0x00, 0x00, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0xf3, 0x00];

// clamp-to-0 name lookup (== C++ toEnum: raw > maxValid -> 0)
const name = <T extends readonly string[]>(names: T, raw: number): T[number] =>
  names[raw < names.length ? raw : 0];
const idx = <T extends readonly string[]>(names: T, v: string): number => names.indexOf(v as T[number]);

// ---- decode helpers -------------------------------------------------------------

// On-disk command byte -> Command name. fmt>=8 inserts B at slot 1 (shifts the
// rest up by one); fmt<8 stores the raw enum (no B).
function decodeCommand(b: number, fmt: number): Command {
  if (fmt < 8) return b <= 23 ? CommandNames[b] : "None";
  if (b === 0) return "None";
  if (b === 1) return "B";
  const v = b - 1;
  return v <= 23 ? CommandNames[v] : "None";
}

function decodeVibrato(v: BitView, base: number, fmt: number): Vibrato {
  const direction = name(VibratoDirectionNames, v.bits(base + 5, 0, 1));
  if (fmt >= 4) {
    const shape = name(VibratoShapeNames, v.bits(base + 5, 1, 2));
    const b5 = v.u8(base + 5);
    const plvSpeed = b5 & 0x80 ? "Step" : b5 & 0x10 ? "Tick" : "Fast";
    return { shape, direction, plvSpeed };
  }
  // fmt<4: shape and PLV speed share byte5[1,2].
  switch (v.bits(base + 5, 1, 2)) {
    case 0:
      return { shape: "Triangle", direction, plvSpeed: "Fast" };
    case 1:
      return { shape: "Sawtooth", direction, plvSpeed: "Tick" };
    case 2:
      return { shape: "Triangle", direction, plvSpeed: "Tick" };
    default:
      return { shape: "Square", direction, plvSpeed: "Tick" };
  }
}

function decodeAdsr(v: BitView, base: number): Adsr {
  return {
    initialLevel: v.bits(base + 1, 4, 4),
    attackSpeed: v.bits(base + 1, 0, 4), // bits 0-2 period + bit 3 direction
    attackLevel: v.bits(base + 9, 4, 4),
    decaySpeed: v.bits(base + 9, 0, 4),
    sustainLevel: v.bits(base + 0xa, 4, 4),
    releaseSpeed: v.bits(base + 0xa, 0, 4),
  };
}

// InstrCommon flatten fields (panning, table?, tableMode) — table omitted when off.
function decodeCommon(v: BitView, base: number): { panning: Instrument["panning"]; table?: number; tableMode: Instrument["tableMode"] } {
  const common: { panning: Instrument["panning"]; table?: number; tableMode: Instrument["tableMode"] } = {
    panning: name(PanningNames, v.bits(base + 7, 0, 2)),
    tableMode: v.bits(base + 5, 3, 1) === 1 ? "Step" : "Play",
  };
  if (v.bits(base + 6, 5, 1) === 1) common.table = v.bits(base + 6, 0, 4); // enabled
  return common;
}

// Pulse/noise length: bit6==0 => infinite (undefined -> omitted); else (~bits[0,5]) & 0x3F.
function decodeLength(v: BitView, base: number): number | undefined {
  if (v.bits(base + 3, 6, 1) === 0) return undefined;
  return (~v.bits(base + 3, 0, 5)) & 0x3f;
}

function decodeInstrument(v: BitView, base: number, fmt: number): Instrument {
  const type = v.u8(base + 0);
  const transpose = v.bits(base + 5, 5, 1) === 0; // stored inverted

  switch (type) {
    case 1: {
      // WAVE
      const common = decodeCommon(v, base);
      const playMode =
        fmt >= 10
          ? name(WavePlayModeNames, (v.bits(base + 9, 0, 2) + 3) & 3) // (raw-1)&3
          : name(WavePlayModeNames, v.bits(base + 9, 0, 2));
      const length =
        fmt >= 7 ? 0xf - v.bits(base + 0xa, 0, 4) : fmt === 6 ? v.bits(base + 0xa, 0, 4) : v.bits(base + 0xe, 4, 4);
      // C++ casts to Byte (uint8_t) — u8+4 can overflow (e.g. 255+4 -> 3), so truncate.
      const speed =
        (fmt >= 7 ? v.u8(base + 0xb) + 4 : fmt === 6 ? v.u8(base + 0xb) + 1 : v.bits(base + 0xe, 0, 4) + 1) & 0xff;
      const loopPos = fmt >= 9 ? v.bits(base + 2, 0, 4) : v.bits(base + 2, 0, 4) ^ 0x0f;
      return {
        type: "wave",
        name: "",
        ...common,
        vibrato: decodeVibrato(v, base, fmt),
        transpose,
        volume: v.u8(base + 1),
        synth: fmt >= 16 ? v.bits(base + 3, 4, 4) : v.bits(base + 2, 4, 4),
        wave: v.u8(base + 3),
        playMode,
        length,
        speed,
        loopPos,
        commandRate: v.u8(base + 8),
      };
    }
    case 2: {
      // KIT
      const common = decodeCommon(v, base);
      return {
        type: "kit",
        name: "",
        ...common,
        volume: v.u8(base + 1),
        kit1: v.bits(base + 2, 0, 5),
        kit2: v.bits(base + 9, 0, 5),
        halfSpeed: v.bits(base + 2, 6, 1) === 1,
        loop1: v.bits(base + 2, 7, 1) ? "Attack" : v.bits(base + 5, 6, 1) ? "On" : "Off",
        loop2: v.bits(base + 9, 7, 1) ? "Attack" : v.bits(base + 5, 5, 1) ? "On" : "Off",
        distortion: name(KitDistortionNames, v.bits(base + 0xa, 0, 2)),
        pitch: v.u8(base + 8),
        length1: v.u8(base + 3),
        offset1: v.u8(base + 0xc),
        offset2: v.u8(base + 0xd),
      };
    }
    case 3: {
      // NOISE
      const common = decodeCommon(v, base);
      const inst: Instrument = {
        type: "noise",
        name: "",
        ...common,
        adsr: decodeAdsr(v, base),
        vibrato: decodeVibrato(v, base, fmt),
        stability: name(NoiseStabilityNames, v.bits(base + 2, 0, 1)),
        shape: v.u8(base + 4),
        commandRate: v.u8(base + 8),
      };
      const length = decodeLength(v, base);
      if (length !== undefined) inst.length = length;
      return inst;
    }
    default: {
      // PULSE (type 0 and any unknown -> pulse)
      const common = decodeCommon(v, base);
      const inst: Instrument = {
        type: "pulse",
        name: "",
        ...common,
        adsr: decodeAdsr(v, base),
        vibrato: decodeVibrato(v, base, fmt),
        transpose,
        pulseWidth: name(PulseWidthNames, v.bits(base + 7, 6, 2)),
        finetune: v.bits(base + 7, 2, 4),
        pulse2Tune: v.u8(base + 2),
        sweep: v.u8(base + 4),
        commandRate: v.u8(base + 8),
      };
      const length = decodeLength(v, base);
      if (length !== undefined) inst.length = length;
      return inst;
    }
  }
}

function allocBit(v: BitView, tableOff: number, index: number): boolean {
  return (v.u8(tableOff + (index >> 3)) & (1 << (index & 7))) !== 0;
}

// ---- encode helpers (exact inverse of the decode helpers) -----------------------

function encodeCommand(c: Command, fmt: number): number {
  const ci = idx(CommandNames, c);
  if (fmt < 8) return ci; // raw enum (B doesn't occur on fmt<8)
  if (c === "None") return 0;
  if (c === "B") return 1;
  return ci + 1;
}

function encodeVibrato(w: BitWriter, base: number, vib: Vibrato, fmt: number): void {
  w.setBits(base + 5, 0, 1, idx(VibratoDirectionNames, vib.direction));
  if (fmt >= 4) {
    w.setBits(base + 5, 1, 2, idx(VibratoShapeNames, vib.shape));
    w.setBits(base + 5, 7, 1, vib.plvSpeed === "Step" ? 1 : 0);
    w.setBits(base + 5, 4, 1, vib.plvSpeed === "Tick" ? 1 : 0);
  } else {
    // fmt<4: (shape,plv) pack into byte5[1,2] — inverse of decode's switch.
    let bits = 0;
    if (vib.plvSpeed === "Tick") bits = vib.shape === "Sawtooth" ? 1 : vib.shape === "Triangle" ? 2 : 3;
    w.setBits(base + 5, 1, 2, bits);
  }
}

function encodeAdsr(w: BitWriter, base: number, a: Adsr): void {
  w.setBits(base + 1, 4, 4, a.initialLevel);
  w.setBits(base + 1, 0, 4, a.attackSpeed);
  w.setBits(base + 9, 4, 4, a.attackLevel);
  w.setBits(base + 9, 0, 4, a.decaySpeed);
  w.setBits(base + 0xa, 4, 4, a.sustainLevel);
  w.setBits(base + 0xa, 0, 4, a.releaseSpeed);
}

function encodeCommon(w: BitWriter, base: number, panning: string, table: number | undefined, tableMode: string): void {
  w.setBits(base + 7, 0, 2, idx(PanningNames, panning));
  w.setBits(base + 5, 3, 1, tableMode === "Step" ? 1 : 0);
  if (table !== undefined) {
    w.setBits(base + 6, 5, 1, 1);
    w.setBits(base + 6, 0, 4, table);
  } else {
    w.setBits(base + 6, 5, 1, 0);
  }
}

function encodeLength(w: BitWriter, base: number, length: number | undefined): void {
  if (length === undefined) {
    w.setBits(base + 3, 6, 1, 0); // infinite
  } else {
    w.setBits(base + 3, 6, 1, 1);
    w.setBits(base + 3, 0, 5, (~length) & 0x1f);
  }
}

function encodeInstrument(w: BitWriter, base: number, inst: Instrument, fmt: number): void {
  if (inst.type === "wave") {
    w.setU8(base + 0, 1);
    encodeCommon(w, base, inst.panning, inst.table, inst.tableMode);
    encodeVibrato(w, base, inst.vibrato, fmt);
    w.setBits(base + 5, 5, 1, inst.transpose ? 0 : 1);
    w.setU8(base + 1, inst.volume);
    w.setU8(base + 3, inst.wave); // wave = full byte 3
    // synth: byte3 hi-nibble (fmt>=16, written after wave) else byte2 hi-nibble.
    w.setBits(fmt >= 16 ? base + 3 : base + 2, 4, 4, inst.synth);
    w.setBits(base + 9, 0, 2, fmt >= 10 ? (idx(WavePlayModeNames, inst.playMode) + 1) & 3 : idx(WavePlayModeNames, inst.playMode));
    if (fmt >= 7) w.setBits(base + 0xa, 0, 4, 0xf - inst.length);
    else if (fmt === 6) w.setBits(base + 0xa, 0, 4, inst.length);
    else w.setBits(base + 0xe, 4, 4, inst.length);
    if (fmt >= 7) w.setU8(base + 0xb, inst.speed - 4);
    else if (fmt === 6) w.setU8(base + 0xb, inst.speed - 1);
    else w.setBits(base + 0xe, 0, 4, inst.speed - 1);
    w.setBits(base + 2, 0, 4, fmt >= 9 ? inst.loopPos : inst.loopPos ^ 0x0f);
    w.setU8(base + 8, inst.commandRate);
  } else if (inst.type === "kit") {
    w.setU8(base + 0, 2);
    encodeCommon(w, base, inst.panning, inst.table, inst.tableMode);
    w.setU8(base + 1, inst.volume);
    w.setBits(base + 2, 0, 5, inst.kit1);
    w.setBits(base + 9, 0, 5, inst.kit2);
    w.setBits(base + 2, 6, 1, inst.halfSpeed ? 1 : 0);
    w.setBits(base + 2, 7, 1, inst.loop1 === "Attack" ? 1 : 0);
    w.setBits(base + 5, 6, 1, inst.loop1 === "On" ? 1 : 0);
    w.setBits(base + 9, 7, 1, inst.loop2 === "Attack" ? 1 : 0);
    w.setBits(base + 5, 5, 1, inst.loop2 === "On" ? 1 : 0);
    w.setBits(base + 0xa, 0, 2, idx(KitDistortionNames, inst.distortion));
    w.setU8(base + 8, inst.pitch);
    w.setU8(base + 3, inst.length1);
    w.setU8(base + 0xc, inst.offset1);
    w.setU8(base + 0xd, inst.offset2);
  } else if (inst.type === "noise") {
    w.setU8(base + 0, 3);
    encodeCommon(w, base, inst.panning, inst.table, inst.tableMode);
    encodeAdsr(w, base, inst.adsr);
    encodeVibrato(w, base, inst.vibrato, fmt);
    w.setBits(base + 2, 0, 1, idx(NoiseStabilityNames, inst.stability));
    encodeLength(w, base, inst.length);
    w.setU8(base + 4, inst.shape);
    w.setU8(base + 8, inst.commandRate);
  } else {
    // PULSE
    w.setU8(base + 0, 0);
    encodeCommon(w, base, inst.panning, inst.table, inst.tableMode);
    encodeAdsr(w, base, inst.adsr);
    encodeVibrato(w, base, inst.vibrato, fmt);
    w.setBits(base + 5, 5, 1, inst.transpose ? 0 : 1);
    w.setBits(base + 7, 6, 2, idx(PulseWidthNames, inst.pulseWidth));
    w.setBits(base + 7, 2, 4, inst.finetune);
    w.setU8(base + 2, inst.pulse2Tune);
    w.setU8(base + 4, inst.sweep);
    encodeLength(w, base, inst.length);
    w.setU8(base + 8, inst.commandRate);
  }
}

// ---- decode / encode ------------------------------------------------------------

export function decodeSong(songBytes: Uint8Array): Song {
  if (songBytes.length < R.kSongByteCount) throw new Error("song body smaller than 0x8000 bytes");
  const v = new BitView(songBytes);
  const fmt = v.u8(R.kFormatVersionOff);
  const r = R.regions(fmt);

  const settingsSyncByte = v.u8(r.syncMode);
  const tempoByte = v.u8(r.tempo);
  const song: Song = {
    formatVersion: fmt,
    settings: {
      tempo: tempoByte < 40 ? tempoByte + 256 : tempoByte,
      transposition: v.u8(r.transposition),
      syncMode: SYNC_FROM_BYTE.get(settingsSyncByte) ?? SYNC_FROM_BYTE.get(0)!, // raw byte; corpus is always valid
      cloneMode: name(CloneModeNames, v.u8(r.cloneMode)),
      font: v.u8(r.font),
      colorPalette: v.u8(r.colorPalette),
      keyDelay: v.u8(r.keyDelay),
      keyRepeat: v.u8(r.keyRepeat),
      prelisten: v.u8(r.prelisten) === 1,
      drumMax: v.u8(r.drumMax),
    },
    rows: [],
    chains: [],
    phrases: [],
    instruments: [],
    tables: [],
    grooves: [],
    synths: [],
    waves: [],
    bookmarks: [],
    words: [],
    wordNames: [],
    instrumentNames: [],
    synthOverwrites: [],
    reserved3FC6: [],
  };

  // SONG-screen grid: rows x channels -> chain index (0xFF = empty)
  for (let row = 0; row < R.kSongRowCount; row++) {
    const chains: (number | null)[] = [];
    for (let ch = 0; ch < R.kChannelCount; ch++) {
      const c = v.u8(r.chainAssignments + row * R.kChannelCount + ch);
      chains.push(c !== 0xff ? c : null);
    }
    song.rows.push({ chains });
  }

  // chains (alloc bitset)
  for (let i = 0; i < R.kChainCount; i++) {
    if (!allocBit(v, r.chainAllocations, i)) {
      song.chains.push(null);
      continue;
    }
    const phrases: (number | null)[] = [];
    const transpositions: number[] = [];
    for (let step = 0; step < R.kChainLength; step++) {
      const ph = v.u8(r.chainPhrases + i * R.kChainLength + step);
      phrases.push(ph !== 0xff ? ph : null);
      transpositions.push(v.u8(r.chainTranspositions + i * R.kChainLength + step));
    }
    song.chains.push({ phrases, transpositions });
  }

  // phrases (alloc bitset); array is sized 256, slot 255 stays null
  for (let i = 0; i < R.kPhraseCount; i++) {
    if (!allocBit(v, r.phraseAllocations, i)) {
      song.phrases.push(null);
      continue;
    }
    const notes: number[] = [];
    const instruments: (number | null)[] = [];
    const commands: Command[] = [];
    const commandValues: number[] = [];
    for (let step = 0; step < R.kPhraseLength; step++) {
      const idxOff = i * R.kPhraseLength + step;
      notes.push(v.u8(r.phraseNotes + idxOff));
      const ins = v.u8(r.phraseInstruments + idxOff);
      instruments.push(ins !== 0xff ? ins : null);
      commands.push(decodeCommand(v.u8(r.phraseCommands + idxOff), fmt));
      commandValues.push(v.u8(r.phraseCommandValues + idxOff));
    }
    song.phrases.push({ notes, instruments, commands, commandValues });
  }
  song.phrases.push(null); // slot 255 (addressable count is 255)

  // instruments (1-byte alloc table)
  for (let i = 0; i < R.kInstrumentCount; i++) {
    if (v.u8(r.instrumentAllocTable + i) === 0) {
      song.instruments.push(null);
      continue;
    }
    song.instruments.push(decodeInstrument(v, r.instrumentParams + i * R.kInstrumentBytes, fmt));
  }

  // tables (1-byte alloc table)
  for (let i = 0; i < R.kTableCount; i++) {
    if (v.u8(r.tableAllocTable + i) === 0) {
      song.tables.push(null);
      continue;
    }
    const volumes: number[] = [];
    const transpositions: number[] = [];
    const command1: Command[] = [];
    const command1Values: number[] = [];
    const command2: Command[] = [];
    const command2Values: number[] = [];
    for (let step = 0; step < R.kTableLength; step++) {
      const idxOff = i * R.kTableLength + step;
      volumes.push(v.u8(r.tableEnvelopes + idxOff));
      transpositions.push(v.u8(r.tableTransposition + idxOff));
      command1.push(decodeCommand(v.u8(r.tableCommand1 + idxOff), fmt));
      command1Values.push(v.u8(r.tableCommand1Value + idxOff));
      command2.push(decodeCommand(v.u8(r.tableCommand2 + idxOff), fmt));
      command2Values.push(v.u8(r.tableCommand2Value + idxOff));
    }
    song.tables.push({ volumes, transpositions, command1, command1Values, command2, command2Values });
  }

  // grooves (no alloc bitset; always present)
  for (let i = 0; i < R.kGrooveCount; i++) {
    const steps: number[] = [];
    for (let step = 0; step < R.kGrooveLength; step++) steps.push(v.u8(r.grooves + i * R.kGrooveLength + step));
    song.grooves.push({ steps });
  }

  // synths
  for (let i = 0; i < R.kSynthCount; i++) {
    const b = r.synthParams + i * R.kSynthBytes;
    song.synths.push({
      waveform: name(SynthWaveformNames, v.u8(b + 0)),
      filter: name(SynthFilterNames, v.u8(b + 1)),
      resonanceStart: fmt >= 5 ? (v.u8(b + 2) & 0xf0) >> 4 : v.u8(b + 2) & 0x0f,
      resonanceEnd: v.u8(b + 2) & 0x0f,
      distortion: name(SynthDistortionNames, v.u8(b + 3)),
      phaseCompression: v.u8(b + 4),
      volumeStart: v.u8(b + 5),
      cutoffStart: v.u8(b + 6),
      phaseStart: v.u8(b + 7),
      vshiftStart: v.u8(b + 8),
      volumeEnd: v.u8(b + 9),
      cutoffEnd: v.u8(b + 10),
      phaseEnd: v.u8(b + 11),
      vshiftEnd: v.u8(b + 12),
      limitStart: 0xf - ((v.u8(b + 13) & 0xf0) >> 4),
      limitEnd: 0xf - (v.u8(b + 13) & 0x0f),
    });
  }

  // waves (raw 16-byte frames)
  for (let i = 0; i < R.kWaveSlotCount; i++) {
    const frames: number[] = [];
    for (let b = 0; b < R.kWaveBytes; b++) frames.push(v.u8(r.waves + i * R.kWaveBytes + b));
    song.waves.push({ frames });
  }

  // raw byte regions
  const blob = (off: number, len: number): number[] => {
    const out: number[] = [];
    for (let b = 0; b < len; b++) out.push(v.u8(off + b));
    return out;
  };
  song.bookmarks = blob(r.bookmarks, 0x40);
  song.words = blob(r.words, 0x540);
  song.wordNames = blob(r.wordNames, 0xa8);
  song.instrumentNames = blob(r.instrumentNames, 0x1a6);
  song.synthOverwrites = blob(r.synthOverwrites, 0x02);
  song.reserved3FC6 = blob(r.reserved3FC6, 0x0a);

  return song;
}

export function encodeSong(song: Song, template?: Uint8Array): Uint8Array {
  const out = new Uint8Array(R.kSongByteCount);
  const hasTemplate = template !== undefined && template.length >= R.kSongByteCount;
  if (hasTemplate) out.set(template.subarray(0, R.kSongByteCount));

  const w = new BitWriter(out);
  const fmt = song.formatVersion;
  const r = R.regions(fmt);

  w.setU8(R.kFormatVersionOff, fmt);
  for (const off of [r.rb1, r.rb2, r.rb3]) {
    w.setU8(off, 0x72); // 'r'
    w.setU8(off + 1, 0x62); // 'b'
  }

  // Fresh-sav sentinel fill — ONLY when encoding from scratch. Unallocated
  // chain-phrase and phrase-instrument slots are 0xFF ("none"); allocated loops
  // overwrite their own slots.
  if (!hasTemplate) {
    out.fill(0xff, r.chainPhrases, r.chainTranspositions);
    out.fill(0xff, r.phraseInstruments, r.rb3);
  }

  // settings
  const s = song.settings;
  w.setU8(r.tempo, s.tempo >= 256 ? s.tempo - 256 : s.tempo);
  w.setU8(r.transposition, s.transposition);
  w.setU8(r.syncMode, SYNC_TO_BYTE[s.syncMode]);
  w.setU8(r.cloneMode, idx(CloneModeNames, s.cloneMode));
  w.setU8(r.font, s.font);
  w.setU8(r.colorPalette, s.colorPalette);
  w.setU8(r.keyDelay, s.keyDelay);
  w.setU8(r.keyRepeat, s.keyRepeat);
  w.setU8(r.prelisten, s.prelisten ? 1 : 0);
  w.setU8(r.drumMax, s.drumMax);

  // SONG grid
  for (let row = 0; row < R.kSongRowCount; row++)
    for (let ch = 0; ch < R.kChannelCount; ch++) {
      const c = song.rows[row].chains[ch];
      w.setU8(r.chainAssignments + row * R.kChannelCount + ch, c !== null ? c : 0xff);
    }

  // chains (regenerate 16-byte alloc bitset)
  for (let b = 0; b < 16; b++) w.setU8(r.chainAllocations + b, 0);
  for (let i = 0; i < R.kChainCount; i++) {
    const c = song.chains[i];
    if (!c) continue;
    w.setBits(r.chainAllocations + (i >> 3), i & 7, 1, 1);
    for (let step = 0; step < R.kChainLength; step++) {
      const idxOff = i * R.kChainLength + step;
      w.setU8(r.chainPhrases + idxOff, c.phrases[step] !== null ? (c.phrases[step] as number) : 0xff);
      w.setU8(r.chainTranspositions + idxOff, c.transpositions[step]);
    }
  }

  // phrases (regenerate 32-byte alloc bitset)
  for (let b = 0; b < 32; b++) w.setU8(r.phraseAllocations + b, 0);
  for (let i = 0; i < R.kPhraseCount; i++) {
    const p = song.phrases[i];
    if (!p) continue;
    w.setBits(r.phraseAllocations + (i >> 3), i & 7, 1, 1);
    for (let step = 0; step < R.kPhraseLength; step++) {
      const idxOff = i * R.kPhraseLength + step;
      w.setU8(r.phraseNotes + idxOff, p.notes[step]);
      w.setU8(r.phraseInstruments + idxOff, p.instruments[step] !== null ? (p.instruments[step] as number) : 0xff);
      w.setU8(r.phraseCommands + idxOff, encodeCommand(p.commands[step], fmt));
      w.setU8(r.phraseCommandValues + idxOff, p.commandValues[step]);
    }
  }

  // instruments (1-byte alloc table)
  for (let i = 0; i < R.kInstrumentCount; i++) {
    const base = r.instrumentParams + i * R.kInstrumentBytes;
    const inst = song.instruments[i];
    if (!inst) {
      w.setU8(r.instrumentAllocTable + i, 0);
      // No-template only: unallocated slots carry LSDj's factory default on
      // fmt<=10 (zeroed on >=11).
      if (!hasTemplate) for (let b = 0; b < R.kInstrumentBytes; b++) w.setU8(base + b, fmt < 11 ? kDefaultInstrumentOld[b] : 0);
      continue;
    }
    w.setU8(r.instrumentAllocTable + i, 1);
    encodeInstrument(w, base, inst, fmt);
  }

  // tables (1-byte alloc table)
  for (let i = 0; i < R.kTableCount; i++) {
    const t = song.tables[i];
    if (!t) {
      w.setU8(r.tableAllocTable + i, 0);
      continue;
    }
    w.setU8(r.tableAllocTable + i, 1);
    for (let step = 0; step < R.kTableLength; step++) {
      const idxOff = i * R.kTableLength + step;
      w.setU8(r.tableEnvelopes + idxOff, t.volumes[step]);
      w.setU8(r.tableTransposition + idxOff, t.transpositions[step]);
      w.setU8(r.tableCommand1 + idxOff, encodeCommand(t.command1[step], fmt));
      w.setU8(r.tableCommand1Value + idxOff, t.command1Values[step]);
      w.setU8(r.tableCommand2 + idxOff, encodeCommand(t.command2[step], fmt));
      w.setU8(r.tableCommand2Value + idxOff, t.command2Values[step]);
    }
  }

  // grooves
  for (let i = 0; i < R.kGrooveCount; i++)
    for (let step = 0; step < R.kGrooveLength; step++) w.setU8(r.grooves + i * R.kGrooveLength + step, song.grooves[i].steps[step]);

  // synths
  for (let i = 0; i < R.kSynthCount; i++) {
    const b = r.synthParams + i * R.kSynthBytes;
    const sy = song.synths[i];
    w.setU8(b + 0, idx(SynthWaveformNames, sy.waveform));
    w.setU8(b + 1, idx(SynthFilterNames, sy.filter));
    if (fmt >= 5) {
      w.setBits(b + 2, 4, 4, sy.resonanceStart);
      w.setBits(b + 2, 0, 4, sy.resonanceEnd);
    } else {
      w.setU8(b + 2, sy.resonanceStart & 0x0f); // whole byte, hi cleared
    }
    w.setU8(b + 3, idx(SynthDistortionNames, sy.distortion));
    w.setU8(b + 4, sy.phaseCompression);
    w.setU8(b + 5, sy.volumeStart);
    w.setU8(b + 6, sy.cutoffStart);
    w.setU8(b + 7, sy.phaseStart);
    w.setU8(b + 8, sy.vshiftStart);
    w.setU8(b + 9, sy.volumeEnd);
    w.setU8(b + 10, sy.cutoffEnd);
    w.setU8(b + 11, sy.phaseEnd);
    w.setU8(b + 12, sy.vshiftEnd);
    w.setBits(b + 13, 4, 4, 0xf - sy.limitStart);
    w.setBits(b + 13, 0, 4, 0xf - sy.limitEnd);
  }

  // waves
  for (let i = 0; i < R.kWaveSlotCount; i++)
    for (let b = 0; b < R.kWaveBytes; b++) w.setU8(r.waves + i * R.kWaveBytes + b, song.waves[i].frames[b]);

  // raw byte regions
  const writeBlob = (off: number, data: number[]): void => {
    for (let b = 0; b < data.length; b++) w.setU8(off + b, data[b]);
  };
  writeBlob(r.bookmarks, song.bookmarks);
  writeBlob(r.words, song.words);
  writeBlob(r.wordNames, song.wordNames);
  writeBlob(r.instrumentNames, song.instrumentNames);
  writeBlob(r.synthOverwrites, song.synthOverwrites);
  writeBlob(r.reserved3FC6, song.reserved3FC6);

  return out;
}
