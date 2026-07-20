// The retroplug-cli SDK's public TypeScript interface — the curated contract shipped to consumer repos
// (e.g. evermidi) as `retroplug-cli.d.ts`, sitting next to the pre-bundled `retroplug-cli.js`. A test
// author imports named symbols from the `.js`; tsc (moduleResolution "bundler") resolves the types here.
//
// This is a HAND-CURATED public surface, not a mechanical dump of the whole package — it types exactly
// what a ROM developer touches (drive a system, render, read live core state, assert). It mirrors the
// real signatures in cli/{session,timeline,wav}.ts, testing/harness.ts and src/{backend,audioDriver,
// systemsStore}.ts; tools/build-cli-sdk.mjs copies it verbatim. Keep it in step with cli/sdk.ts (the
// runtime barrel, which re-exports the real implementations and is typechecked by tsc).

// ─── Live-core read types (NES; getApuState / getPpuState / …). Mirror the native reflect-cpp structs. ──

/** One 2A03 square (pulse) channel. `frequency` is decoded Hz; gate "is it sounding" on
 *  `period > 0 && envelopeVolume > 0` — `enabled` is only the $4015 switch. */
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
  envelopeVolume: number;
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
 *  chips; `period` is the chip-native pitch register. `constantOutput` (VRC6 "ignore duty" → DC/no
 *  tone), `instrument` (VRC7 patch) and `volume` are the diagnostic fields. */
export interface ExpansionAudioChannel {
  enabled: boolean;
  volume: number;
  outputLevel: number;
  period: number;
  block: number;
  duty: number;
  constantOutput: boolean;
  instrument: number;
}

/** The decoded NES expansion-audio snapshot (`getExpansionAudioState`). `chip` is the active chip
 *  ("none" when the cart has no expansion sound); `channels` are its voices in chip order. */
export interface ExpansionAudioState {
  chip: "none" | "vrc6" | "vrc7" | "s5b" | "n163" | string;
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
}

/** One named CPU register (`getCpuRegisters`). NES (6502) reports a/x/y/sp/ps (8-bit) + pc (16-bit). */
export interface CpuRegister {
  name: string;
  value: number;
  bits: number;
}

/** One Mesen event-viewer event (`drainEvents`) logged for the last frame. */
export interface DebugEvent {
  type: number;
  operationType: number;
  address: number;
  value: number;
  programCounter: number;
  scanline: number;
  cycle: number;
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

export interface Backend {
  /** Write `bytes` to `path` (e.g. persist a rendered WAV). */
  writeFile(path: string, bytes: Uint8Array): boolean;
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
  fileExists(path: string): boolean;
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
  /** APU/PPU/mapper register-write + event log for the last frame (frame-scoped). */
  drainEvents(id: number): DebugEvent[];
  /** Load a cc65 `.dbg` symbol file so profiling / breakpoints / disassembly / call stack show names. */
  loadLabels(id: number, path: string): boolean;

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
}

// ─── The audio driver (Session.audio): render + drive input/MIDI. Curated ROM-testing subset. ─────────

export interface AudioDriver {
  /** Advance the render `ms` and return interleaved-stereo Float32 PCM @ 44100 (L,R,L,R…). */
  renderAudio(ms: number): Float32Array;
  /** Advance `ms` and return EACH live system's own interleaved-stereo output, in project-slot order. */
  renderAudioPerSystem(ms: number): Float32Array[];
  /** Stage one global host-MIDI message for the kernel's next render (fanned to systems by routing). */
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
  /** Stage a raw MIDI message (≤4 bytes) — global host MIDI, fanned to systems by the routing role. */
  midi(ms: number, bytes: number[]): this;
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

/** Fluent assertions. Any throw fails the case (reported as TAP `not ok`). */
export declare function expect(actual: unknown): {
  toBe(expected: unknown): void;
  toEqual(expected: unknown): void;
  toBeTruthy(): void;
  toBeFalsy(): void;
  toThrow(match?: string | RegExp): void;
};
