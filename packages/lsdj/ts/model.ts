// The LSDj sav model — the single source of truth (zod), ported from the C++
// reflect-cpp structs under packages/native/src/lsdj/model/. Schemas mirror the
// reflect-cpp JSON contract exactly (validated against the frozen golden decode
// JSON): enums are the C++ enumerator NAME strings; optional struct fields
// (table/length) are OMITTED when absent (`.optional()`); optional array cells
// (chain/phrase/instrument/project slots) are `null` (`.nullable()`); every
// field has a default so `Sav.parse({})` yields a full default image (the
// reflect-cpp DefaultIfMissing "author only the cells you set" behaviour).
//
// The codec (codec/song.ts, codec/sav.ts) produces/consumes these shapes
// DIRECTLY as plain objects (no zod on the hot path); zod is for authoring
// (savFromJson) and static types. The enum name arrays double as the codec's
// byte<->name tables (byte == index, except the two sparse maps below).
import { z } from "zod";

// ---- bounded scalars (mirror model/Types.hpp Nibble/U5/U3/U2; reject, not clamp) ----
const nibble = () => z.number().int().min(0).max(15);
const u5 = () => z.number().int().min(0).max(31);
const byte = () => z.number().int().min(0).max(255);
const u16 = () => z.number().int().min(0).max(65535);

// ---- fixed(elem, N): the FixedArray<T,N> reflector -------------------------------
// A short/omitted array pads to N with the element's default; > N is an error.
// `elem` must answer `elem.parse(undefined)` with its default (scalars/enums via
// `.default(v)`, array cells via `.nullable().default(null)`, object cells via
// `.prefault(() => ({}))`), so padding is a fresh value per slot.
export const fixed = <T extends z.ZodTypeAny>(elem: T, n: number) =>
  z
    .array(elem)
    .max(n)
    .default([])
    .transform((a) => {
      const out = a.slice();
      while (out.length < n) out.push(elem.parse(undefined) as z.infer<T>);
      return out;
    });

const nullableByte = () => byte().nullable().default(null);

// ---- enums (name strings, C++ declaration order — the array index IS the byte) ----
export const PanningNames = ["None", "Left", "Right", "LeftRight"] as const;
export const TableModeNames = ["Play", "Step"] as const;
export const PulseWidthNames = ["W125", "W25", "W50", "W75"] as const;
export const VibratoShapeNames = ["Triangle", "Sawtooth", "Square"] as const;
export const VibratoDirectionNames = ["Down", "Up"] as const;
export const PlvSpeedNames = ["Fast", "Tick", "Step", "Drum"] as const;
export const WavePlayModeNames = ["Once", "Loop", "PingPong", "Manual"] as const;
export const KitLoopModeNames = ["Off", "On", "Attack"] as const;
export const KitDistortionNames = ["Clip", "Shape", "Shape2", "Wrap"] as const;
export const NoiseStabilityNames = ["Free", "Stable"] as const;
export const CloneModeNames = ["Deep", "Slim"] as const;
export const SynthWaveformNames = ["Saw", "Square", "Triangle"] as const;
export const SynthFilterNames = ["LowPass", "HighPass", "BandPass", "AllPass"] as const;
export const SynthDistortionNames = ["Clip", "Wrap", "Fold"] as const;
// SyncMode: the on-disk byte is LSDj's own sparse encoding (bytes 2 and 4 unused).
export const SyncModeNames = ["None", "Lsdj", "Midi", "Keyboard", "AnalogIn", "AnalogOut", "MidiMap", "MidiOut"] as const;
export const SYNC_TO_BYTE: Record<string, number> = { None: 0, Lsdj: 1, Midi: 3, Keyboard: 5, AnalogIn: 6, AnalogOut: 7, MidiMap: 8, MidiOut: 9 };
export const SYNC_FROM_BYTE = new Map<number, (typeof SyncModeNames)[number]>(
  (Object.entries(SYNC_TO_BYTE) as [(typeof SyncModeNames)[number], number][]).map(([k, v]) => [v, k]),
);
// Command: semantic (in-memory) order — B is last (index 23). The on-disk byte
// differs by format version (fmt>=8 inserts B at slot 1); song.ts owns that remap.
export const CommandNames = [
  "None", "A", "C", "D", "E", "F", "G", "H", "K", "L", "M", "O", "P", "R", "S", "T", "V", "W", "Z", "N", "X", "Q", "Y", "B",
] as const;

// ---- sub-structs ----------------------------------------------------------------
export const AdsrSchema = z
  .object({
    initialLevel: nibble().default(0),
    attackSpeed: nibble().default(0),
    attackLevel: nibble().default(0),
    decaySpeed: nibble().default(0),
    sustainLevel: nibble().default(0),
    releaseSpeed: nibble().default(0),
  })
  .prefault(() => ({}));

export const VibratoSchema = z
  .object({
    shape: z.enum(VibratoShapeNames).default("Triangle"),
    direction: z.enum(VibratoDirectionNames).default("Down"),
    plvSpeed: z.enum(PlvSpeedNames).default("Fast"),
  })
  .prefault(() => ({}));

// InstrCommon is rfl::Flatten — its fields sit INLINE in each instrument object.
const instrCommon = {
  panning: z.enum(PanningNames).default("None"),
  table: nibble().optional(), // omitted when the table is off (NOT null)
  tableMode: z.enum(TableModeNames).default("Play"),
};

export const PulseInstrumentSchema = z.object({
  type: z.literal("pulse"),
  name: z.string().default(""),
  ...instrCommon,
  adsr: AdsrSchema,
  vibrato: VibratoSchema,
  transpose: z.boolean().default(true),
  pulseWidth: z.enum(PulseWidthNames).default("W125"),
  finetune: nibble().default(0),
  pulse2Tune: byte().default(0),
  sweep: byte().default(0),
  length: byte().optional(), // omitted = infinite
  commandRate: byte().default(0),
});

export const WaveInstrumentSchema = z.object({
  type: z.literal("wave"),
  name: z.string().default(""),
  ...instrCommon,
  vibrato: VibratoSchema,
  transpose: z.boolean().default(true),
  volume: byte().default(0xa8),
  synth: nibble().default(0),
  wave: byte().default(0),
  playMode: z.enum(WavePlayModeNames).default("Once"),
  length: nibble().default(0), // wave length is always present
  speed: byte().default(0),
  loopPos: nibble().default(0),
  commandRate: byte().default(0),
});

export const KitInstrumentSchema = z.object({
  type: z.literal("kit"),
  name: z.string().default(""),
  ...instrCommon,
  volume: byte().default(0xa8),
  kit1: u5().default(0),
  kit2: u5().default(0),
  halfSpeed: z.boolean().default(false),
  loop1: z.enum(KitLoopModeNames).default("Off"),
  loop2: z.enum(KitLoopModeNames).default("Off"),
  distortion: z.enum(KitDistortionNames).default("Clip"),
  pitch: byte().default(0),
  length1: byte().default(0),
  offset1: byte().default(0),
  offset2: byte().default(0),
});

export const NoiseInstrumentSchema = z.object({
  type: z.literal("noise"),
  name: z.string().default(""),
  ...instrCommon,
  adsr: AdsrSchema,
  vibrato: VibratoSchema,
  stability: z.enum(NoiseStabilityNames).default("Free"),
  length: byte().optional(), // omitted = infinite
  shape: byte().default(0),
  commandRate: byte().default(0),
});

export const InstrumentSchema = z.discriminatedUnion("type", [
  PulseInstrumentSchema,
  WaveInstrumentSchema,
  KitInstrumentSchema,
  NoiseInstrumentSchema,
]);
const nullableInstrument = () => InstrumentSchema.nullable().default(null);

export const PhraseSchema = z
  .object({
    notes: fixed(byte().default(0), 16),
    instruments: fixed(nullableByte(), 16),
    commands: fixed(z.enum(CommandNames).default("None"), 16),
    commandValues: fixed(byte().default(0), 16),
  })
  .prefault(() => ({}));

export const ChainSchema = z
  .object({
    phrases: fixed(nullableByte(), 16),
    transpositions: fixed(byte().default(0), 16),
  })
  .prefault(() => ({}));

export const TableSchema = z
  .object({
    volumes: fixed(byte().default(0), 16),
    transpositions: fixed(byte().default(0), 16),
    command1: fixed(z.enum(CommandNames).default("None"), 16),
    command1Values: fixed(byte().default(0), 16),
    command2: fixed(z.enum(CommandNames).default("None"), 16),
    command2Values: fixed(byte().default(0), 16),
  })
  .prefault(() => ({}));

// Groove default is LSDj's factory 6/6 (a zero groove never advances under sync).
export const GrooveSchema = z
  .object({ steps: fixed(byte().default(0), 16).prefault([6, 6]) })
  .prefault(() => ({}));

export const SynthSchema = z
  .object({
    waveform: z.enum(SynthWaveformNames).default("Saw"),
    filter: z.enum(SynthFilterNames).default("LowPass"),
    resonanceStart: nibble().default(0),
    resonanceEnd: nibble().default(0),
    distortion: z.enum(SynthDistortionNames).default("Clip"),
    phaseCompression: byte().default(0),
    volumeStart: byte().default(0),
    cutoffStart: byte().default(0),
    phaseStart: byte().default(0),
    vshiftStart: byte().default(0),
    volumeEnd: byte().default(0),
    cutoffEnd: byte().default(0),
    phaseEnd: byte().default(0),
    vshiftEnd: byte().default(0),
    limitStart: nibble().default(0),
    limitEnd: nibble().default(0),
  })
  .prefault(() => ({}));

export const WaveSchema = z.object({ frames: fixed(byte().default(0), 16) }).prefault(() => ({}));

export const SongSettingsSchema = z
  .object({
    tempo: u16().default(128),
    transposition: byte().default(0),
    syncMode: z.enum(SyncModeNames).default("None"),
    cloneMode: z.enum(CloneModeNames).default("Deep"),
    font: byte().default(0),
    colorPalette: byte().default(0),
    keyDelay: byte().default(0),
    keyRepeat: byte().default(0),
    prelisten: z.boolean().default(false),
    drumMax: byte().default(0),
  })
  .prefault(() => ({}));

export const SongRowSchema = z.object({ chains: fixed(nullableByte(), 4) }).prefault(() => ({}));

export const SongSchema = z
  .object({
    formatVersion: byte().default(22),
    settings: SongSettingsSchema,
    rows: fixed(SongRowSchema, 256),
    chains: fixed(ChainSchema.nullable().default(null), 128),
    phrases: fixed(PhraseSchema.nullable().default(null), 256),
    instruments: fixed(nullableInstrument(), 64),
    tables: fixed(TableSchema.nullable().default(null), 32),
    grooves: fixed(GrooveSchema, 32),
    synths: fixed(SynthSchema, 16),
    waves: fixed(WaveSchema, 256),
    bookmarks: fixed(byte().default(0), 0x40),
    words: fixed(byte().default(0), 0x540),
    wordNames: fixed(byte().default(0), 0xa8),
    instrumentNames: fixed(byte().default(0), 0x1a6),
    synthOverwrites: fixed(byte().default(0), 0x02),
    reserved3FC6: fixed(byte().default(0), 0x0a),
  })
  .prefault(() => ({}));

export const StoredProjectSchema = z
  .object({
    name: z.string().default(""),
    version: byte().default(0),
    song: SongSchema,
  })
  .prefault(() => ({}));

export const SavSchema = z.object({
  activeProjectIndex: byte().default(0xff),
  reserved: fixed(byte().default(0), 30),
  workingSong: SongSchema,
  projects: fixed(StoredProjectSchema.nullable().default(null), 32),
});

// ---- inferred types (the codec's working shapes) --------------------------------
export type Adsr = z.infer<typeof AdsrSchema>;
export type Vibrato = z.infer<typeof VibratoSchema>;
export type Instrument = z.infer<typeof InstrumentSchema>;
export type Phrase = z.infer<typeof PhraseSchema>;
export type Chain = z.infer<typeof ChainSchema>;
export type Table = z.infer<typeof TableSchema>;
export type Groove = z.infer<typeof GrooveSchema>;
export type Synth = z.infer<typeof SynthSchema>;
export type Wave = z.infer<typeof WaveSchema>;
export type SongSettings = z.infer<typeof SongSettingsSchema>;
export type SongRow = z.infer<typeof SongRowSchema>;
export type Song = z.infer<typeof SongSchema>;
export type StoredProject = z.infer<typeof StoredProjectSchema>;
export type Sav = z.infer<typeof SavSchema>;
// The lenient AUTHORING input: every field optional (unset cells default, `.prefault`/`.default`). This
// is what savFrom() accepts — an object literal type-checked against the model, no JSON round-trip.
export type SavInput = z.input<typeof SavSchema>;
export type Command = (typeof CommandNames)[number];
