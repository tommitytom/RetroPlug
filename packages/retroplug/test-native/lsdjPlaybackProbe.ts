// An instrument for observing what a REAL LSDj cart does in SYNC=MI.MAP, byte by byte.
//
// The playback predictor (docs/launchpad-plan.md, M0) has to know LSDj's map-mode semantics exactly:
// whether the cart advances past a launched row on its own, whether one clock byte is one tick,
// what the 0xFE handshake does. None of that is documented — the aboy build is closed-source and the
// LSDj manual doesn't cover MI.MAP at all — so we measure it instead of inferring it.
//
// Two things make this an instrument rather than a test:
//
//   1. It drives the protocol DIRECTLY. The system runs `lsdj-sync` in `midiPassthrough`, which
//      forwards every host-MIDI byte verbatim into the link port (dspRoles.ts forwardMidiToSerial),
//      so `raw()` puts an exact byte on the wire. That measures THE CART, not RetroPlug's `midiMap`
//      role — which matters here, because whether that role is complete is one of the open questions.
//      `probeViaRole()` builds the other configuration (the shipped `midiMap` role) for the one
//      experiment that is about our code.
//
//   2. It samples position per render slice via the per-block WRAM seam (backend.readRam →
//      LsdjReader, the same path LsdjOverlay uses) and returns a TIMELINE, so a caller can watch how
//      state evolves rather than probing a single settled point.
//
// Every entry point returns null when the aboy ROM is absent so callers skip cleanly, matching the
// other LSDj native tests.

import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { LsdjReader, type LsdjState, CHANNELS, type ChannelName } from "../src/lsdj/runtime";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

/** The aboy build is the one that has MI.MAP at all (docs/lsdj.md ROM table). */
export const ABOY_ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";

// The MI.MAP wire protocol, as Arduinoboy's Mode_LSDJ_Map.ino writes it to the link port. Row bytes
// are the payload; these two are the sentinels that aren't rows.
export const MAP_CLOCK = 0xff; // one per MIDI clock (0xF8) — Arduinoboy's setMapByte(0xFF, …)
export const MAP_NOTEOFF = 0xfe; // the NoteOff handshake (MIDIMAP_NOTEOFF in dspRoles.ts)

/** One 24-PPQN tick at ~119 BPM. LSDj drains the link port from its frame loop (~16.7 ms), so a
 *  clock stream faster than a frame is simply not consumed — the first version of this probe clocked
 *  at 3-5 ms and the cart never moved at all. Keep tick spacing at or above one frame. */
export const TICK_MS = 21;

/** Milliseconds to render after a launch byte before clocking, so the cart has frames in which to
 *  act on it. Without this the first clocks race the row trigger. */
export const SETTLE_MS = 50;

/** One observation: the decoded runtime state at a point in rendered time. */
export interface ProbeSample {
  ms: number; // cumulative rendered milliseconds at this sample
  ticks: number; // clock bytes written to the cart so far
  playing: boolean;
  songRow: number | null;
  screen: string;
  channels: Record<ChannelName, { playing: boolean; songRow: number | null; chain: number | null; chainRow: number | null; phrase: number | null; phraseRow: number | null }>;
}

export interface ProbeOptions {
  song: SavInput;
  /** `lsdj-sync` mode for the system's pipeline. Default `midiPassthrough` (raw byte control). */
  mode?: string;
  rom?: string;
  /** Milliseconds rendered before the probe hands back control, to clear the boot/self-test. */
  bootMs?: number;
}

const emptyChannels = (): ProbeSample["channels"] => ({
  pu1: { playing: false, songRow: null, chain: null, chainRow: null, phrase: null, phraseRow: null },
  pu2: { playing: false, songRow: null, chain: null, chainRow: null, phrase: null, phraseRow: null },
  wav: { playing: false, songRow: null, chain: null, chainRow: null, phrase: null, phraseRow: null },
  noi: { playing: false, songRow: null, chain: null, chainRow: null, phrase: null, phraseRow: null },
});

// Starts high on purpose. Every test in a file shares one native host process, and neighbouring tests
// routinely construct systems with hand-written low ids (id = 1 is the convention), so a counter
// starting at 1 would silently adopt another test's cart instead of building its own.
let nextSystemId = 100;

/** A live LSDj cart plus the means to write bytes at it and watch what happens. */
export class LsdjProbe {
  private ms = 0;
  private tickCount = 0;

  private constructor(
    private readonly be: ReturnType<typeof createRealBackend>,
    private readonly audio: ReturnType<typeof createAudioDriver>,
    private readonly reader: LsdjReader,
    private readonly id: number,
  ) {}

  /** Boot a cart with `song` in its battery. Null when the ROM is missing or its version has no
   *  WRAM layout (the reader would then report nothing useful, so there is no experiment to run). */
  static create(opts: ProbeOptions): LsdjProbe | null {
    const rom = opts.rom ?? ABOY_ROM;
    const be = createRealBackend();
    if (!be.fileExists(rom)) return null;

    const header = be.readFilePrefix(rom, 0x150);
    if (!header) return null;
    const reader = LsdjReader.fromHeader(header);
    if (!reader.supported) return null;

    const audio = createAudioDriver();
    const dsp = createDspRuntime();
    // A fresh id per probe: every test in this file shares ONE native host process, so reusing an id
    // would collide with the cart a previous test left running (TS owns system identity - doc-01).
    const id = nextSystemId++;
    if (!be.constructSystem({
      romPath: rom, platform: "gb", core: "sameboy", embeddedRom: "",
      savPath: null, statePath: null, sramBytes: savFrom(opts.song),
    }, id)) return null;

    if (!dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)) return null;
    if (!dsp.setSystems({
      project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
      systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode: opts.mode ?? "midiPassthrough" } }] }],
    })) return null;

    const probe = new LsdjProbe(be, audio, reader, id);
    probe.render(opts.bootMs ?? 6000); // past the cartridge self-test, onto the song screen
    return probe;
  }

  /** Put one raw byte on the link port (via the passthrough role). A 1-byte "MIDI message" whose
   *  status nibble routing broadcasts unchanged — the routing layer never rewrites it, and
   *  forwardMidiToSerial pushes exactly these bytes. */
  raw(byte: number): void {
    const ok = this.audio.stageMidiIn([byte & 0xff]);
    if (!ok && !this.warnedRaw) {
      console.log(`  !! stageMidiIn rejected a 1-byte message (0x${byte.toString(16)}) - the raw path is not delivering`);
      this.warnedRaw = true;
    }
  }
  private warnedRaw = false;

  /** Launch song row `row` (0..255). Written as the bare row byte, which is what Arduinoboy sends,
   *  then rendered a little so the cart can act on it before any clock arrives. */
  launchRaw(row: number, settleMs = SETTLE_MS): ProbeSample {
    this.raw(row);
    return this.render(settleMs);
  }

  /** The NoteOff handshake for whichever row is currently sounding. */
  releaseRaw(): void {
    this.raw(MAP_NOTEOFF);
  }

  /** Put an arbitrary host-MIDI message on the wire. For driving the cart with bytes some OTHER module
   *  produced - the controller layer's launch encoder, say - rather than with this probe's own idea of
   *  what the protocol looks like. */
  stage(data: readonly number[]): void {
    this.audio.stageMidiIn([...data]);
  }

  /** Launch a row THROUGH THE MIDI LAYER, as a Launchpad app would: ch1 note = rows 0..127,
   *  ch2 note = rows 128..255 (midiMapRow in dspRoles.ts). Only meaningful in `midiMap` mode. */
  launchNote(row: number, on = true): void {
    const status = (on ? 0x90 : 0x80) | (row >= 128 ? 1 : 0);
    this.audio.stageMidiIn([status, row & 0x7f, on ? 100 : 0]);
  }

  /** Feed `count` clock bytes, rendering `msPerTick` between each, and sample after every tick.
   *  LSDj counts clock BYTES rather than wall time, so `msPerTick` is not the musical tempo — but it
   *  cannot be arbitrarily small either: LSDj services the link port from its frame loop, so bytes
   *  pushed much faster than ~16.7 ms apart are not consumed. The default is one 24-PPQN tick at
   *  ~119 BPM, which is both realistic and comfortably above a frame. */
  runTicks(count: number, msPerTick = TICK_MS): ProbeSample[] {
    const out: ProbeSample[] = [];
    for (let i = 0; i < count; i++) {
      this.raw(MAP_CLOCK);
      this.tickCount++;
      out.push(this.render(msPerTick));
    }
    return out;
  }

  /** Render `ms` with no clock, sampling every `stepMs` — for watching a cart that is free-running. */
  runFree(ms: number, stepMs = 20): ProbeSample[] {
    const out: ProbeSample[] = [];
    for (let t = 0; t < ms; t += stepMs) out.push(this.render(stepMs));
    return out;
  }

  /** Render `ms` and return the state at the end of it. */
  render(ms: number): ProbeSample {
    this.audio.renderAudio(ms);
    this.ms += ms;
    return this.sample();
  }

  /** Decode the current WRAM without advancing time. */
  sample(): ProbeSample {
    const wram = this.be.readRam(this.id);
    const s: ProbeSample = {
      ms: this.ms, ticks: this.tickCount, playing: false, songRow: null,
      screen: "unknown", channels: emptyChannels(),
    };
    if (!wram) return s;
    const st: LsdjState = this.reader.read(wram);
    s.playing = st.playing;
    s.songRow = st.songRow;
    s.screen = st.screen;
    for (const ch of CHANNELS) {
      const c = st.channels[ch];
      s.channels[ch] = {
        playing: c.playing, songRow: c.songRow, chain: c.chain,
        chainRow: c.chainRow, phrase: c.phrase, phraseRow: c.phraseRow,
      };
    }
    return s;
  }

  state(): LsdjState | null {
    const wram = this.be.readRam(this.id);
    return wram ? this.reader.read(wram) : null;
  }

  transport(on: boolean): void {
    this.audio.setTransport(on);
  }

  /** Host tempo, for the role-generated clock path (eachTick derives ticks from bpm + ppq). */
  bpm(bpm: number): void {
    this.audio.setBpm(bpm);
  }

  systemId(): number {
    return this.id;
  }
}

// --- timeline helpers (what the experiments actually assert / print on) --------------------------

/** The distinct values a field takes over a timeline, in order, collapsing runs. Turns "row was 0 for
 *  40 samples then 1 for 40" into [0, 1] — the shape every advance question is really asking about. */
export function transitions<T>(samples: ProbeSample[], pick: (s: ProbeSample) => T): T[] {
  const out: T[] = [];
  for (const s of samples) {
    const v = pick(s);
    if (out.length === 0 || out[out.length - 1] !== v) out.push(v);
  }
  return out;
}

/** Rendered-millisecond points at which `pick` changed value. Use this (not changeTicks) when the
 *  clock is generated by the ROLE rather than fed byte-by-byte here, since `ticks` only counts bytes
 *  this probe wrote. */
export function changeMs<T>(samples: ProbeSample[], pick: (s: ProbeSample) => T): number[] {
  const out: number[] = [];
  let prev: T | undefined;
  let first = true;
  for (const s of samples) {
    const v = pick(s);
    if (!first && v !== prev) out.push(s.ms);
    prev = v;
    first = false;
  }
  return out;
}

/** Tick counts at which `pick` changed value — the raw material for "how many ticks per step". */
export function changeTicks<T>(samples: ProbeSample[], pick: (s: ProbeSample) => T): number[] {
  const out: number[] = [];
  let prev: T | undefined;
  let first = true;
  for (const s of samples) {
    const v = pick(s);
    if (!first && v !== prev) out.push(s.ticks);
    prev = v;
    first = false;
  }
  return out;
}

/** Gaps between successive change points — 6 ticks/step at the default groove would read [6,6,6,…]. */
export function gaps(points: number[]): number[] {
  const out: number[] = [];
  for (let i = 1; i < points.length; i++) out.push(points[i] - points[i - 1]);
  return out;
}

/** A compact one-line dump of a sample, for the experiment logs. */
export function fmtSample(s: ProbeSample): string {
  const ch = CHANNELS.map((c) => {
    const v = s.channels[c];
    return `${c}=${v.playing ? "P" : "-"}${v.songRow ?? "_"}/${v.chain ?? "_"}:${v.chainRow ?? "_"}.${v.phraseRow ?? "_"}`;
  }).join(" ");
  return `t=${s.ticks} ms=${s.ms} ${s.playing ? "PLAY" : "stop"} row=${s.songRow ?? "_"} [${ch}]`;
}
