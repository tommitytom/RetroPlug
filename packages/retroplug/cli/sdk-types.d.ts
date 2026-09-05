// The retroplug-cli SDK's public TypeScript interface — the curated contract shipped to consumer repos
// (e.g. bliptoaster) as `retroplug-cli.d.ts`, sitting next to the pre-bundled `retroplug-cli.js`. A test
// author imports named symbols from the `.js`; tsc (moduleResolution "bundler") resolves the types here.
//
// This is a HAND-CURATED public surface, not a mechanical dump of the whole package — it types exactly
// what a ROM developer touches (drive a system, render, read live core state, assert). It mirrors the
// real signatures in cli/{session,timeline,wav}.ts, testing/harness.ts and src/{backend,audioDriver,
// systemsStore}.ts; tools/build-cli-sdk.mjs copies it verbatim. Keep it in step with cli/sdk.ts (the
// runtime barrel, which re-exports the real implementations and is typechecked by tsc).

// ─── Live-core read types (NES; getApuState / getPpuState / …). Mirror the native reflect-cpp structs. ──

/** One 2A03 square (pulse) channel. `frequency` is decoded Hz; gate "is it sounding" on
 *  `period > 0 && envelopeOutput > 0` — `enabled` is only the $4015 switch.
 *
 *  The envelope, three ways. `envelopeVolume` is the $4000 low nibble as written: the level in
 *  constant-volume mode, but the DECAY PERIOD in hardware-envelope mode (`constantVolume` false), where a
 *  ROM wanting the fastest decay writes 0 and the field reads 0 while a note is plainly audible.
 *  `envelopeLevel` is the envelope unit's live decay counter (15 at (re)trigger, counting down, wrapping
 *  back to 15 while `envelopeLoop` is set). `envelopeOutput` is what the mixer actually multiplies by:
 *  `envelopeVolume` in constant mode, `envelopeLevel` in envelope mode, 0 once the length counter has run
 *  out - "the note kept its level" reads from this one. */
export interface ApuSquareState {
  enabled: boolean;
  period: number;
  timer: number;
  duty: number;
  outputVolume: number;
  frequency: number;
  lengthCounter: number;
  constantVolume: boolean;
  envelopeVolume: number;
  envelopeLevel: number;
  envelopeOutput: number;
  envelopeLoop: boolean;
  sweepEnabled: boolean;
  sweepNegate: boolean;
  sweepPeriod: number;
  sweepShift: number;
}

/** The triangle channel — fixed amplitude (no envelope/velocity). Gate on `period > 0 && lengthCounter > 0`. */
export interface ApuTriangleState {
  enabled: boolean;
  period: number;
  timer: number;
  outputVolume: number;
  frequency: number;
  lengthCounter: number;
  linearCounter: number;
}

export interface ApuNoiseState {
  enabled: boolean;
  period: number;
  timer: number;
  outputVolume: number;
  frequency: number;
  lengthCounter: number;
  modeFlag: boolean;
  constantVolume: boolean;
  /** The $400C low nibble as written; see ApuSquareState for the three envelope fields. */
  envelopeVolume: number;
  envelopeLevel: number;
  envelopeOutput: number;
  envelopeLoop: boolean;
}

export interface ApuDmcState {
  enabled: boolean;
  sampleAddr: number;
  sampleLength: number;
  bytesRemaining: number;
  period: number;
  outputVolume: number;
  loop: boolean;
  irqEnabled: boolean;
  sampleRate: number;
}

/** The decoded NES APU snapshot (`getApuState`). */
export interface ApuState {
  pulse1: ApuSquareState;
  pulse2: ApuSquareState;
  triangle: ApuTriangleState;
  noise: ApuNoiseState;
  dmc: ApuDmcState;
}

/** One expansion-audio voice (`getExpansionAudioState`). Superset across chips — a field is populated
 *  when meaningful and 0/false otherwise. `volume` is normalized 0 (silent) .. 15 (loudest) across all
 *  chips; `period` is the chip-native pitch register. `frequency` is the decoded output pitch in Hz (the
 *  expansion analogue of `ApuSquareState.frequency`) - computed from the pitch register regardless of
 *  audibility, so gate "sounding" on `enabled`/`volume` and read `frequency` for "what pitch"; it is 0
 *  only when the pitch is undefined (a zero timer/fnum). Every pitch/volume field is the PROGRAMMED
 *  register - read straight from the chip's register file, so two reads of one held note agree; only
 *  `outputLevel` is live. `waveLength`/`activeChannels`/`waveAddress` are the N163's other pitch terms
 *  and its wave-RAM start (the same values `N163VoiceWrite` reconstructs from the write log; 0 for other
 *  chips). `constantOutput` (VRC6 "ignore duty" → DC/no tone), `instrument` (VRC7 patch) and `volume`
 *  are the diagnostic fields. */
export interface ExpansionAudioChannel {
  enabled: boolean;
  volume: number;
  outputLevel: number;
  period: number;
  frequency: number;
  block: number;
  duty: number;
  constantOutput: boolean;
  instrument: number;
  waveLength: number;
  activeChannels: number;
  waveAddress: number;
}

/** The decoded NES expansion-audio snapshot (`getExpansionAudioState`). `chip` is the active chip
 *  ("none" when the cart has no expansion sound); `channels` are its voices in chip order. MMC5 reports
 *  [pulse1, pulse2, pcm]: the pulses like the 2A03's (`period`/`frequency`/`duty`, `volume` = the
 *  envelope's effective output 0-15, 0 once the length counter is out), and the PCM channel's 8-bit
 *  $5011 DAC level in `outputLevel` (`volume` = that level scaled to 0-15; `enabled` while write mode is
 *  selected). */
export interface ExpansionAudioState {
  chip: "none" | "vrc6" | "vrc7" | "s5b" | "n163" | "mmc5" | string;
  channels: ExpansionAudioChannel[];
}

/** The NES PPU state snapshot (`getPpuState`). */
export interface PpuState {
  scanline: number;
  cycle: number;
  frameCount: number;
  control: number;
  mask: number;
  status: number;
  scrollX: number;
  videoRamAddr: number;
  tmpVideoRamAddr: number;
  writeToggle: boolean;
  spriteRamAddr: number;
  /** 32-byte palette RAM ($3F00-$3F1F): [0] = universal background color, [1..] the bg/sprite palettes. */
  paletteRam: Uint8Array;
}

/** One named CPU register (`getCpuRegisters`). NES (6502) reports a/x/y/sp/ps (8-bit) + pc (16-bit). */
export interface CpuRegister {
  name: string;
  value: number;
  bits: number;
}

/** One Mesen event-viewer event (`drainEvents`): a register access / NMI / IRQ / DMA read. `frame` is
 *  the event manager's own frame counter (it increments where the log rotates, the pre-render line - NOT
 *  `PpuState.frameCount`, which increments 21 scanlines earlier), so `frame:scanline:cycle` is a stable
 *  identity for an event across polls. `type` is the DebugEventType ordinal (0=Register, 1=Nmi, 2=Irq,
 *  3=Breakpoint, 4=BgColorChange, 5=SpriteZeroHit, 6=DmcDmaRead, 7=DmaRead); `operationType` the
 *  MemoryOperationType ordinal (0=Read, 1=Write, ...); `value` is -1 for a read with no captured value. */
export interface DebugEvent {
  type: number;
  operationType: number;
  address: number;
  value: number;
  programCounter: number;
  scanline: number;
  cycle: number;
  frame: number;
}

// ─── Debugger + profiler types (breakpoints / stepping / trace / profiling / disassembly). ──────────────

/** A breakpoint to install via `setBreakpoints`. `type` is "execute" (break when PC enters `[start,end]`)
 *  / "read" / "write" (break on a CPU access to the range). `end` defaults to `start`; `condition` is an
 *  optional Mesen expression (e.g. "Y == 0"), empty for unconditional. */
export interface Breakpoint {
  type: "execute" | "read" | "write";
  start: number;
  end?: number;
  condition?: string;
}

/** Result of a step (`stepInto`/`stepOver`/`stepOut`) or `runUntilBreak`. `broke` is false when nothing
 *  fired (no debug target / cycle cap); `pc` is the triggering / new PC; `breakpointId` is the hit
 *  breakpoint index, or -1 for a step / the cap. */
export interface BreakInfo {
  broke: boolean;
  pc: number;
  breakpointId: number;
}

/** One row of the execution trace (`readTrace`, most-recent first). `pc` is the instruction address;
 *  `text` is the disassembly + register state Mesen logged for it. */
export interface TraceLine {
  pc: number;
  text: string;
}

/** One function's profiler sample (`readProfile`). `address` is the ROM offset; `label` is the resolved
 *  symbol (empty until `loadLabels`). `exclusiveCycles` is time in the function itself (the bottleneck
 *  signal); `inclusiveCycles` counts callees too. */
export interface ProfiledFunction {
  address: number;
  label: string;
  exclusiveCycles: number;
  inclusiveCycles: number;
  callCount: number;
  minCycles: number;
  maxCycles: number;
}

/** One disassembled instruction (`disassemble`). `text` is the mnemonic + operands (symbol-resolved once
 *  labels load); `bytes` is the machine bytes as hex. */
export interface DisasmLine {
  address: number;
  text: string;
  bytes: string;
}

/** One call-stack frame (`getCallStack`, outermost first). `label` is empty until `loadLabels`. */
export interface CallFrame {
  address: number;
  label: string;
}

// ─── The backend: live-core reads + file I/O a ROM test uses (the full Backend has more; this is the
//     ROM-testing subset). All reads are valid on the control thread with the audio thread NOT started. ──

/** A decoded RGBA8888 image (`pngDecode`); `rgba` is `width*height*4` bytes, row-major, top-to-bottom. */
export interface PngImageData {
  width: number;
  height: number;
  rgba: Uint8Array;
}

export interface Backend {
  /** Write `bytes` to `path` (e.g. persist a rendered WAV). */
  writeFile(path: string, bytes: Uint8Array): boolean;
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
  fileExists(path: string): boolean;
  readFile(path: string): Uint8Array | null;
  /** Encode an RGBA8888 buffer to PNG bytes (native lodepng); null on failure. Used by the spectrogram. */
  pngEncode(width: number, height: number, rgba: Uint8Array): Uint8Array | null;
  /** Decode PNG bytes to RGBA8888; null if not a valid/supported PNG. */
  pngDecode(bytes: Uint8Array): PngImageData | null;
  /** Decoded NES APU snapshot. NES-only (zeroed on other cores). */
  getApuState(id: number): ApuState;
  /** Decoded NES expansion-audio snapshot (VRC6/VRC7/S5B/N163). `chip` is "none" without expansion
   *  audio. `volume` is normalized 0 (silent) .. 15 (loudest) across chips. */
  getExpansionAudioState(id: number): ExpansionAudioState;
  getPpuState(id: number): PpuState;
  /** Debugger-style single-byte read/write at a CPU address. */
  readCpu(id: number, addr: number): number | null;
  writeCpu(id: number, addr: number, value: number): boolean;
  /** Read a whole memory region (region selector mirrors native `rp::MemoryType`). */
  readMemory(id: number, region: number): Uint8Array | null;
  getCpuRegisters(id: number): CpuRegister[];
  /** Set a single 6502 register (a/x/y/sp/ps/pc) by name. */
  setCpuRegister(id: number, name: string, value: number): boolean;
  /** The APU/PPU/mapper register-access + NMI/IRQ/DMA event log: every event that is NEW since the last
   *  call, oldest first, each stamped with its `frame`. Mesen retains only the frame in progress plus the
   *  previous one, so poll at least once per PPU frame (~16 ms) to see everything - a slower poll loses
   *  whole frames, never repeats. The first call returns both retained frames. */
  drainEvents(id: number): DebugEvent[];
  /** Load a cc65 `.dbg` symbol file so profiling / breakpoints / disassembly / call stack show names. */
  loadLabels(id: number, path: string): boolean;
  /** The CPU address of a symbol from the loaded `.dbg`, by name: a C name (`g_frame`, and a file-scope
   *  `static` such as `s_mode1`) or the assembler label (`_g_frame`, `midiIdleLoop`). Null until
   *  `loadLabels`, or for an unknown name. Pair with `readCpu` to read a ROM variable by name. */
  symbolAddress(id: number, name: string): number | null;

  // ── Debugger: breakpoints, stepping, trace, disassembly, call stack (NES-only) ──
  /** Run one 6502 instruction; returns the cycles it took. */
  stepInstruction(id: number): number;
  /** Install breakpoints (replaces any existing; empty array clears). Drive with runUntilBreak, not runMs. */
  setBreakpoints(id: number, breakpoints: Breakpoint[]): boolean;
  /** Step the CPU until a breakpoint fires or `maxCycles` elapses. */
  runUntilBreak(id: number, maxCycles: number): BreakInfo;
  /** Run until PC reaches `target` (or `maxCycles`) — a one-shot execute breakpoint. */
  runUntilPc(id: number, target: number, maxCycles: number): boolean;
  /** Source-level stepping. */
  stepInto(id: number): BreakInfo;
  stepOver(id: number): BreakInfo;
  stepOut(id: number): BreakInfo;
  /** Toggle the execution trace logger; read the captured rows with readTrace. */
  setTrace(id: number, on: boolean): boolean;
  readTrace(id: number, count: number): TraceLine[];
  /** Disassemble `count` instructions from `addr` (symbol-resolved once labels load). */
  disassemble(id: number, addr: number, count: number): DisasmLine[];
  /** The current call stack (outermost first). */
  getCallStack(id: number): CallFrame[];

  // ── Profiler: per-function cycle counts (load labels first for names) ──
  /** Start/reset function-level profiling. */
  beginProfile(id: number): boolean;
  /** Read the accumulated per-function samples (exclusive/inclusive cycles, call counts). */
  readProfile(id: number): ProfiledFunction[];
}

// ─── Loading systems (Session.project.systems). ───────────────────────────────────────────────────────

/** loadRom outcome: defer to a sibling `.rplg` project, a built system, or failure. */
export type LoadResult = { deferredProject: string } | { system: number } | null;

export interface SystemsStore {
  /** Load exactly `romPath` as a fresh system and project it into DSP so audio renders; returns the
   *  system id handle (or null on failure). The ROM-test loader — no sibling-`.rplg` deferral, no
   *  stray disk writes. */
  addSystem(romPath: string, opts?: { explicitSav?: string }): number | null;
  /** The full loader (may defer to a sibling `<rom>.rplg`). */
  loadRom(romPath: string, opts?: { explicitSav?: string }): LoadResult;
  /** Load the embedded mGB Game Boy synth (no external file). */
  loadMgb(): number | null;
  removeSystem(id: number): boolean;
  /** Edit one of the system's role configs (validated by the role's schema; a "system" role's config is
   *  applied to the live core). E.g. the NES console region: `setRoleConfig(id, "mesen", { region: "pal" })`
   *  - baked at construct, so follow it with `reset(id)` to reboot in that region. False when the system
   *  or the role is absent. */
  setRoleConfig(id: number, roleKind: string, partial: Record<string, unknown>): boolean;
  /** Reboot `id` in place, carrying its battery SRAM forward (a hardware-style reset: the save persists,
   *  the running state is dropped). Rebuilds the core, so the system gets a NEW id - use the returned one;
   *  null when `id` is absent. */
  reset(id: number): number | null;
}

// ─── The audio driver (Session.audio): render + drive input/MIDI. Curated ROM-testing subset. ─────────

export interface AudioDriver {
  /** Advance the render `ms` and return interleaved-stereo Float32 PCM @ 44100 (L,R,L,R…). */
  renderAudio(ms: number): Float32Array;
  /** Advance `ms` and return EACH live system's own interleaved-stereo output, in project-slot order. */
  renderAudioPerSystem(ms: number): Float32Array[];
  /** Stage global host-MIDI bytes for the kernel's next render - any non-empty length: a channel message
   *  (fanned to systems by routing), a whole SysEx, or several messages as one run (broadcast unchanged,
   *  delivered to the N8 FIFO byte-for-byte, in order). False only for an empty array. */
  stageMidiIn(bytes: Uint8Array | number[]): boolean;
  /** Drain the MIDI-out the DSP kernel emitted since the last drain (each entry is one message). */
  drainMidiOut(): { system: number; frame: number; data: Uint8Array }[];
  /** Enqueue a button transition (down = press/release); a press+release around a short render is a tap. */
  pressButton(id: number, button: number, down: boolean): boolean;
  /** Write a system's latest framebuffer to `path` as an RGB24 PNG. */
  screenshot(id: number, path: string): boolean;
  setTransport(running: boolean): boolean;
  setBpm(bpm: number): boolean;
  /** Set the host sample rate (Hz). Only takes effect BEFORE any system is built (baked into cores at
   *  construct); returns false once a system exists or for a non-positive rate. */
  setSampleRate(sampleRate: number): boolean;
}

// ─── The composition root a session drives (bootSession()). ──────────────────────────────────────────

/** Everything a session drives: the wired control plane over the real backend. `registry`/`recent`/`dsp`
 *  are wired for you and rarely touched directly in a ROM test. */
export interface Session {
  backend: Backend;
  project: { systems: SystemsStore };
  audio: AudioDriver;
  registry: unknown;
  recent: unknown;
  dsp: unknown;
}

/** Stand up the control plane the way every host does (backend + stores + DSP kernel loaded). */
export declare function bootSession(): Session;

/** The session's argument vector — everything after the session `.js` on the CLI
 *  (`retroplug-cli <session.js> [args...]`). Empty when absent. */
export declare function hostArgs(): string[];

// ─── Timed-event scripting (Timeline + renderTimeline). ──────────────────────────────────────────────

/** Note options — `channel` is 1-based (default 1); `velocity` 0..127 (default 100). */
export interface NoteOpts {
  channel?: number;
  velocity?: number;
}

/** A flat, absolute-ms event the player fires (authors use the builder methods, not this directly). */
export type TimelineEvent =
  | { ms: number; kind: "midi"; bytes: number[] }
  | { ms: number; kind: "press"; system: number; button: number; down: boolean }
  | { ms: number; kind: "bpm"; bpm: number }
  | { ms: number; kind: "transport"; running: boolean }
  | { ms: number; kind: "screenshot"; system: number; path: string }
  | { ms: number; kind: "at"; fn: (s: Session) => void };

/** A fluent, TS-authored timeline of timed emulator events. Every method records an event at absolute
 *  `ms` and returns `this`. */
export declare class Timeline {
  /** Stage raw MIDI bytes — global host MIDI, fanned to systems by the routing role. Any length: one
   *  channel message, a whole SysEx, or several messages as one run (a run longer than one message is
   *  broadcast unchanged and reaches the N8 FIFO byte-for-byte, in order). Throws at authoring time on an
   *  empty array or a non-byte value, so a bad message can never be dropped silently. */
  midi(ms: number, bytes: number[]): this;
  /** A System Exclusive message: `payload` (7-bit bytes, manufacturer id first) wrapped in F0 .. F7.
   *  Throws at authoring time if a payload byte has bit 7 set. */
  sysex(ms: number, payload: number[]): this;
  noteOn(ms: number, note: number, opts?: NoteOpts): this;
  noteOff(ms: number, note: number, opts?: NoteOpts): this;
  /** noteOn at `ms`, noteOff at `ms + durationMs`. */
  note(ms: number, note: number, opts: NoteOpts & { durationMs: number }): this;
  press(ms: number, system: number, button: number, down: boolean): this;
  /** Tap `button` on `system`: down at `ms`, up at `ms + holdMs` (default 50). */
  tap(ms: number, system: number, button: number, opts?: { holdMs?: number }): this;
  bpm(ms: number, bpm: number): this;
  transport(ms: number, running: boolean): this;
  screenshot(ms: number, system: number, path: string): this;
  /** Run `fn` against the live Session at `ms` — the render advances to `ms` first, so `fn` observes the
   *  core at exactly that time. The observe/assert hook (read APU/CPU/memory and `expect` on it). */
  at(ms: number, fn: (s: Session) => void): this;
  /** The events flattened to a stable ms-sorted list (insertion order breaks ties). */
  build(): TimelineEvent[];
}

/** Play `timeline` against a booted session and return the concatenated interleaved-stereo PCM.
 *  `warmupMs` renders + DISCARDS that many ms first to boot the core (n8-midi needs ~1s). */
export declare function renderTimeline(
  session: Session,
  timeline: Timeline,
  opts: { durationMs: number; warmupMs?: number },
): Float32Array;

/** Named button values (Right=0..Start=7) + the GBA-only L/R wire bytes. Pass to Timeline.press/tap. */
export declare const Button: Record<string, number>;

// ─── WAV encoding. ───────────────────────────────────────────────────────────────────────────────────

/** Encode interleaved PCM (as renderAudio returns it) to a 16-bit RIFF/PCM WAV. */
export declare function encodeWav(pcm: Float32Array, sampleRate?: number, channels?: number): Uint8Array;

// ─── TAP test harness. Register cases at top level; they run on a microtask, print TAP, set exit code. ──

/** Register a test case. The harness auto-runs on a microtask after the module body finishes and owns
 *  TAP output + the process exit code — do NOT wrap the body in a runSession()-style exit. */
export declare function test(name: string, fn: () => void | Promise<void>): void;

/** Fluent assertions. Any throw fails the case (reported as TAP `not ok`). Every failure names both
 *  values (`expected > 50, got 12`), and the optional `message` is prefixed so a case with several
 *  checks says which one fired: `expect(hz, "pulse1 pitch").toBeCloseTo(440, 1)`. */
export declare function expect(actual: unknown, message?: string): {
  toBe(expected: unknown): void;
  toEqual(expected: unknown): void;
  toBeTruthy(): void;
  toBeFalsy(): void;
  toBeGreaterThan(bound: number): void;
  toBeGreaterThanOrEqual(bound: number): void;
  toBeLessThan(bound: number): void;
  toBeLessThanOrEqual(bound: number): void;
  /** |actual - expected| <= tolerance (an ABSOLUTE tolerance, default 1e-9 - not jest's digit count). */
  toBeCloseTo(expected: number, tolerance?: number): void;
  toThrow(match?: string | RegExp): void;
};

// ─── DSP core (F2): FFT, windows, magnitude spectrum over interleaved-stereo @44100 render output. ──────

export declare const DEFAULT_SAMPLE_RATE: number;
export type WindowType = "hann" | "hamming" | "blackman" | "rect";
export interface Spectrum {
  freqs: Float32Array;
  mag: Float32Array;
  sampleRate: number;
  binHz: number;
}
/** Smallest power of two >= n. */
export declare function nextPow2(n: number): number;
/** In-place radix-2 FFT; `re`/`im` must be the same power-of-two length. */
export declare function fft(re: Float64Array, im: Float64Array): void;
/** Window coefficients of length `n` (periodic form). */
export declare function windowCoeffs(type: WindowType, n: number): Float64Array;
/** De-interleave 2-channel PCM to mono (default: L+R average). */
export declare function toMono(pcm: Float32Array, opts?: { channel?: "left" | "right" | "mix" }): Float32Array;
/** A mono window of `n` samples starting at `startMs`, from interleaved-stereo PCM (zero-filled past end). */
export declare function window(pcm: Float32Array, startMs: number, n: number, sampleRate?: number): Float32Array;
/** Windowed magnitude spectrum of a mono signal (Hann by default), bins 0..N/2. */
export declare function magnitudeSpectrum(x: Float32Array, opts?: { window?: WindowType; sampleRate?: number }): Spectrum;

// ─── Pitch detection (F3): FFT + Harmonic Product Spectrum. Prefer decoded Hz (F1) when available. ──────

export interface PitchResult {
  hz: number;          // fundamental Hz; 0 when no confident pitch
  cents: number;       // signed cents from the nearest equal-tempered note; NaN when hz==0
  confidence: number;  // 0..1
  harmonics: number;   // how many harmonics reinforced the estimate
}
/** Signed cents of a measured vs expected frequency: 1200*log2(m/e). NOT octave-folded (catches octave errors). */
export declare function centsError(measuredHz: number, expectedHz: number): number;
/** Fundamental via FFT + HPS. Low default fmin so octave errors are detected, not filtered. Reliable for
 *  2A03 pulse/triangle + harmonic tones; a strongly inharmonic FM timbre (VRC7) can lock onto a partial, so
 *  for expansion-audio tuning prefer the decoded Hz (getExpansionAudioState().frequency). */
export declare function detectPitch(x: Float32Array, opts?: { sampleRate?: number; fmin?: number; fmax?: number; harmonics?: number }): PitchResult;

// ─── Timbre / quality metrics (F5). ────────────────────────────────────────────────────────────────────

/** Magnitude-weighted mean frequency in Hz ("brightness"). */
export declare function spectralCentroid(x: Float32Array, sampleRate?: number): number;
/** Sum of the magnitudes at the first `n` harmonics of `f0`. */
export declare function harmonicEnergy(x: Float32Array, f0: number, n?: number, sampleRate?: number): number;
/** Total harmonic distortion at `f0`: sqrt(sum(H2..Hn^2)) / H1. */
export declare function thd(x: Float32Array, f0: number, n?: number, sampleRate?: number): number;
/** Noise-floor level in dB: 20*log10(median/peak). Lower = cleaner. */
export declare function noiseFloorDb(x: Float32Array, sampleRate?: number): number;
/** Absolute spectral power in the [loHz, hiHz] band, in dB. */
export declare function bandEnergyDb(x: Float32Array, loHz: number, hiHz: number, sampleRate?: number): number;

// ─── Spectrogram (F4): STFT + a magma-style PNG via the host's native pngEncode. Inputs are MONO. ───────

export interface StftOpts {
  fftSize?: number;
  hopMs?: number;
  sampleRate?: number;
  window?: WindowType;
}
export interface Stft {
  times: Float32Array;
  freqs: Float32Array;
  magDb: Float32Array[];
  binHz: number;
}
export interface SpectrogramOpts extends StftOpts {
  fmax?: number;
  logFreq?: boolean;
  db?: [number, number];
  width?: number;
  height?: number;
}
export interface RgbaImage { width: number; height: number; rgba: Uint8Array; }
export interface PngWriter {
  pngEncode(width: number, height: number, rgba: Uint8Array): Uint8Array | null;
  writeFile(path: string, bytes: Uint8Array): boolean;
}
/** STFT of a MONO signal; magDb normalized so the loudest bin is 0 dB. Bridge from a render with toMono. */
export declare function stft(mono: Float32Array, opts?: StftOpts): Stft;
/** Render a MONO signal's spectrogram to an RGBA image (time X, frequency Y, low at bottom). Pure. */
export declare function spectrogramImage(mono: Float32Array, opts?: SpectrogramOpts): RgbaImage;
/** Render a MONO signal's spectrogram and write it as a PNG via the host's native pngEncode. */
export declare function writeSpectrogramPng(backend: PngWriter, mono: Float32Array, path: string, opts?: SpectrogramOpts): boolean;

// ─── Expansion-audio register decode (F6): the drainEvents write log -> programmed per-voice values. ────

export type ExpansionChip = "vrc6" | "vrc7" | "s5b" | "n163";
export interface Vrc6VoiceWrite {
  channel: number; kind: "pulse" | "saw"; freqReg: number; enabled: boolean;
  volume: number; duty: number; ignoreDuty: boolean; freqShift: number; haltAudio: boolean;
}
export interface Vrc7VoiceWrite { channel: number; fnum: number; block: number; key: boolean; inst: number; volume: number; }
export interface S5bVoiceWrite { channel: number; period: number; volume: number; toneEnabled: boolean; }
export interface N163VoiceWrite {
  channel: number; enabled: boolean; freqReg: number; waveLen: number; waveAddr: number; volume: number; numChannels: number;
}
export interface DecodedExpansionWrites {
  vrc6?: Vrc6VoiceWrite[];
  vrc7?: Vrc7VoiceWrite[];
  s5b?: S5bVoiceWrite[];
  n163?: N163VoiceWrite[];
}
/** Reconstruct the final programmed expansion-audio registers from a drainEvents write log. Pairs with F1. */
export declare function decodeExpansionWrites(events: DebugEvent[], chip: ExpansionChip): DecodedExpansionWrites;

// ─── Tuning / timbre assertions (F7): built on F1 (decoded Hz) + F3 (detectPitch). Throw on failure. ────

/** Assert a measured pitch is within `tolCents` of `expectedHz` (feed decoded Hz F1 or detectPitch F3). */
export declare function assertInTune(measuredHz: number, expectedHz: number, opts?: { tolCents?: number }): void;
/** Detect a mono buffer's pitch and assert it is within `tolCents` of `expectedHz`. */
export declare function assertPitchInTune(mono: Float32Array, expectedHz: number, opts?: { tolCents?: number; sampleRate?: number; fmin?: number; fmax?: number; minConfidence?: number }): void;
/** A stable spectral fingerprint (normalized log-band dB vector) for golden-audio regression. */
export declare function spectralFingerprint(mono: Float32Array, opts?: { bands?: number; sampleRate?: number; fmin?: number; fmax?: number }): number[];
/** Assert a render's fingerprint has not drifted from `golden` by more than `tol` dB in any band. */
export declare function assertFingerprint(mono: Float32Array, golden: number[], tol?: number, opts?: { bands?: number; sampleRate?: number; fmin?: number; fmax?: number }): void;
