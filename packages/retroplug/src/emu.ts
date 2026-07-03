// `emu` — the domain ergonomics layer over the generated, typed HarnessService
// client (reflect-cpp -> OpenRPC -> TS), dispatched synchronously through an
// in-process __rpcSend hook (createSyncClient). It keeps stable signatures
// (ArrayBuffer/Uint8Array/Float32Array shapes) and does the binary reshaping the
// wire can't express directly.
//
// `createEmu(rpcSend)` builds an `emu` over any sync transport, so both the CLI
// test harness (test/harness/index.ts) and the end-user CLI (packages/cli)
// share one facade. It has no test-runner or window dependency.

import { createSyncClient, type RpcSend } from "./createSyncClient";
import type { HarnessService } from "harness-service";

// Name-keyed CPU register file. The set differs per backend — every supported
// system includes a "pc". SameBoy: af,bc,de,hl,sp,pc. NES: a,x,y,sp,ps,pc.
// GBA: r0..r15,cpsr,pc. (See src/system/CpuState.hpp.)
export type CpuRegisters = Record<string, number>;

// One function's profiler sample (emulated cycles). `label` is the symbol name
// once labels are loaded, else "". Sorted by exclusiveCycles (the bottleneck
// signal: time spent in the function itself, excluding callees).
export interface ProfiledFunction {
  address: number;
  label: string;
  exclusiveCycles: number;
  inclusiveCycles: number;
  callCount: number;
  minCycles: number;
  maxCycles: number;
}

export interface DisasmLine { address: number; text: string; bytes: string; }
export interface TraceLine { pc: number; text: string; }
export interface CallFrame { address: number; label: string; }

// -- NES APU per-channel state (Mesen NES only) ------------------------------
//
// A snapshot of the five NES APU channels' DECODED state. The raw $4000-$4013
// APU registers are write-only on the NES (they read back as open bus), so a
// MIDI-driven ROM's effect on the sound chip can't be seen via readMemory — this
// is how you observe it. `frequency` is the derived pitch in Hz. `outputVolume`
// is the instantaneous, duty/sequence-gated mixer output (it oscillates). A
// *sounding note* shows as `period` > 0 and (square/noise) `envelopeVolume` > 0;
// `enabled` is only the channel's $4015 on/off switch, which a ROM often sets
// once at init, so it is NOT "a note is sounding". (`period` == 0 makes
// `frequency` meaninglessly high, not 0 — gate on `period`/`outputVolume`.)
export interface ApuSquareState {
  enabled: boolean;        // $4015 channel switch (not "a note is sounding")
  period: number;          // raw 11-bit timer reload
  timer: number;
  duty: number;            // 0-3
  outputVolume: number;    // live 0-15, duty-gated
  frequency: number;       // Hz
  lengthCounter: number;
  constantVolume: boolean;
  envelopeVolume: number;  // the set volume 0-15
  sweepEnabled: boolean;
  sweepNegate: boolean;
  sweepPeriod: number;
  sweepShift: number;
}
export interface ApuTriangleState {
  enabled: boolean;
  period: number;
  timer: number;
  outputVolume: number;    // triangle step 0-15 (no volume control)
  frequency: number;
  lengthCounter: number;
  linearCounter: number;
}
export interface ApuNoiseState {
  enabled: boolean;
  period: number;          // period index -> pitch
  timer: number;
  outputVolume: number;
  frequency: number;
  lengthCounter: number;
  modeFlag: boolean;       // metallic/periodic vs normal noise
  constantVolume: boolean;
  envelopeVolume: number;
}
export interface ApuDmcState {
  enabled: boolean;        // bytes remaining > 0 (sample playing)
  sampleAddr: number;      // $C000-$FFFF start
  sampleLength: number;
  bytesRemaining: number;
  period: number;
  outputVolume: number;    // 7-bit DAC level 0-127
  loop: boolean;
  irqEnabled: boolean;
  sampleRate: number;      // Hz
}
/** The five NES APU channels' decoded state (pulse1/pulse2/triangle/noise/dmc). */
export interface ApuState {
  pulse1: ApuSquareState;
  pulse2: ApuSquareState;
  triangle: ApuTriangleState;
  noise: ApuNoiseState;
  dmc: ApuDmcState;
}

// One MIDI message a role emitted back to the host (e.g. Arduinoboy MI.OUT).
// `sample` is the absolute sample position since the system was loaded.
export interface MidiOutEvent { sample: number; bytes: number[]; }
// One raw GB serial-out byte captured in Arduinoboy master mode.
export interface SerialOutByte { sample: number; byte: number; }
// One sample slot in an LSDj kit patch: a source audio file + 3-char slot name.
export interface KitSample { path: string; name: string; }

export interface BreakpointSpec {
  type: "execute" | "read" | "write";
  start: number;
  end?: number;        // defaults to start (single address)
  condition?: string;  // optional Mesen expression, e.g. "A == 0x90"
}
export interface BreakInfo {
  broke: boolean;        // false = hit the cycle cap
  pc: number;            // triggering address (runUntilBreak) / new PC (step)
  breakpointId: number;  // -1 for a step / the cap
}

export interface Frame {
  width: number;
  height: number;
  published: boolean;
  pixels: Uint8Array; // XRGB8888, width*height*4 bytes (empty if !published)
}

export interface ChordOpts { staggerMs?: number; holdMs?: number; }

/** Format profiler results as a hot-function table (for console.log). */
export function printProfile(fns: ProfiledFunction[], top = 20): string {
  const hex = (a: number) => "$" + (a >>> 0).toString(16).padStart(4, "0");
  const lines = fns.slice(0, top).map((f) =>
    `${String(f.exclusiveCycles).padStart(12)}  ${String(f.callCount).padStart(8)}  ` +
    `${f.label || hex(f.address)}`,
  );
  return [`${"exclCycles".padStart(12)}  ${"calls".padStart(8)}  function`, ...lines].join("\n");
}

// -- Button / memory-region / MIDI-routing enums -----------------------------
//
// Hand-mirrored from src/system/InputTypes.hpp (GameboyButton),
// src/system/MemoryType.hpp, and src/project/ProjectConfig.hpp (MidiRouting).
// The wire values are load-bearing; TestHarness.cpp holds matching
// static_assert guards so a C++ renumber breaks the build.

export const Button = {
  Right: 0, Left: 1, Up: 2, Down: 3, A: 4, B: 5, Select: 6, Start: 7,
} as const;

export const Mem = {
  Ram: 0, Rom: 1, Sram: 2, Vram: 3, IORegisters: 4,
  HRam: 5, OAM: 6, NametableRam: 7, ExtWorkRam: 8,
} as const;

export const Routing = {
  SendToAll: 0, FourChannelsPerInstance: 1, OneChannelPerInstance: 2,
  MidiChannelToInstance: 3,
} as const;

export type ButtonId = (typeof Button)[keyof typeof Button];
export type MemType = (typeof Mem)[keyof typeof Mem];
export type RoutingId = (typeof Routing)[keyof typeof Routing];

// -- binary reshaping helpers ------------------------------------------------
//
// rfl::Bytestring decodes from msgpack BIN as a Uint8Array at runtime, but the
// codegen types struct *fields* (HarnessFrame.data, HarnessPerSystemAudio.systems)
// as `string` — hence the `as unknown` casts. Binary INPUT params are number[]
// on the wire (reflect-cpp's reader can't take std::byte), so we widen with
// Array.from.

function asU8(v: unknown): Uint8Array {
  if (v instanceof Uint8Array) return v;
  if (v instanceof ArrayBuffer) return new Uint8Array(v);
  throw new Error("emu: expected binary value");
}
function toNums(buf: ArrayBuffer | Uint8Array): number[] {
  return Array.from(asU8(buf));
}
function toArrayBuffer(v: unknown): ArrayBuffer {
  const u8 = asU8(v);
  return u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength);
}
// Copy into a Uint8Array backed by an exact, zero-offset ArrayBuffer. The
// msgpack decoder hands back BIN as a *view* into the (larger) message buffer,
// so callers that reach for `.buffer` (e.g. readMemory(...).buffer) would see
// the whole frame. The old native facade always returned a fresh copy; match it.
function copyU8(v: unknown): Uint8Array {
  return new Uint8Array(toArrayBuffer(v));
}
function toFloat32(v: unknown): Float32Array {
  return new Float32Array(toArrayBuffer(v));
}

// -- emu facade --------------------------------------------------------------

/** Build an `emu` control surface over a synchronous RPC transport. */
export function createEmu(rpcSend: RpcSend) {
  const client = createSyncClient<HarnessService>(rpcSend);

  const emu = {
    /** Load a Game Boy ROM; returns the new system id. An optional `sav`
     *  ArrayBuffer (e.g. from savFromJson) boots the system from that .sav.
     *  Optional `lsdjSyncMode` ("MidiSync"/"MidiMap"/"MidiPassthrough"/
     *  "ArduinoboyMaster"/...) pre-seeds the LSDj sync role. Optional `linkGroup`
     *  (same nonzero value on two systems) links them for LSDj link-cable sync. */
    loadRom(path: string, sav?: ArrayBuffer, lsdjSyncMode?: string, linkGroup?: number): number {
      return client.loadRom(path, sav ? toNums(sav) : [], lsdjSyncMode ?? "", linkGroup ?? 0);
    },
    /** Build a 128 KiB .sav image from a (possibly partial) Sav-model JSON
     *  fixture — missing fields take model defaults, and short or omitted fixed
     *  arrays pad to their full on-disk length (author only the cells you set).
     *  Feed the result to loadRom(rom, sav) to boot LSDj from an authored song. */
    savFromJson(json: string): ArrayBuffer {
      return toArrayBuffer(client.savFromJson(json));
    },
    /** Overwrite a running system's cartridge battery RAM (e.g. a .sav image).
     *  Mirrors the plugin's Load SRAM minus the reset; pair with reset() to make
     *  the game re-read it on boot. Returns false if the cart has no battery. */
    loadSram(sys: number, sram: ArrayBuffer): boolean {
      return client.loadSram(sys, toNums(sram));
    },
    /** Serialize a system's cartridge battery RAM (e.g. an LSDj .sav) the way the
     *  plugin's Save SRAM does — distinct from readMemory(Sram), the live region.
     *  Returns a copy. */
    saveSram(sys: number): Uint8Array {
      return copyU8(client.saveSram(sys));
    },
    /** Run one SRAM auto-save flush (the same helper the plugin's idle pump
     *  drives): write the sibling `<rom>.sav` if the battery changed since the
     *  last call. Returns true if it wrote; repeat calls with unchanged SRAM
     *  return false. The system must have been loaded from a path. */
    autoSaveSram(sys: number): boolean {
      return client.autoSaveSram(sys);
    },
    /** Soft-reset a system (the GB reset button). After loadSram, the game boots
     *  into the freshly loaded battery RAM. */
    reset(sys: number): void {
      client.reset(sys);
    },
    /** Slurp a file's raw bytes — e.g. a source .sav to pass to loadRom. */
    readFile(path: string): Uint8Array {
      return copyU8(client.readFile(path));
    },
    /** Dump an ArrayBuffer's raw bytes to a file (e.g. an upgraded .sav). */
    writeFile(path: string, bytes: ArrayBuffer): void {
      client.writeFile(path, toNums(bytes));
    },
    /** Best-effort delete a file (no error if absent) — e.g. to simulate a
     *  moved/missing ROM or kit sample WAV before a reload. */
    removeFile(path: string): void {
      client.removeFile(path);
    },
    /** Byte-check a captured .sav: decode its working song, re-encode from the
     *  model with the input as template, and return the first non-volatile diff
     *  offset (or -1 if byte-identical). Catches modeled-region codec bugs. */
    savRoundtripDiff(savBytes: ArrayBuffer): number {
      return client.savRoundtripDiff(toNums(savBytes));
    },
    /** Advance every loaded system by `ms` of emulated time. */
    runMs(ms: number): void {
      client.runMs(ms);
    },
    /** Set a single button's state on a system. */
    press(sys: number, button: number, down: boolean): void {
      client.press(sys, button, down);
    },
    /** Deliver a MIDI message (1-4 bytes, e.g. [0x90, note, vel]) to a system —
     *  queued for the next runMs. On NES it feeds the N8 MIDI FIFO, so profiling
     *  with sendMidi exercises the ROM's note-handling path, not just the idle loop. */
    sendMidi(sys: number, bytes: number[]): void {
      client.sendMidi(sys, bytes);
    },
    /** Route a MIDI message across the loaded systems by `routing` (the channel
     *  nibble picks the target system), unlike sendMidi which targets one system.
     *  Default routing is SendToAll (the CLI's `midi_routing` modes). */
    dispatchMidi(bytes: number[], routing: number = Routing.SendToAll): void {
      client.dispatchMidi(bytes, routing);
    },
    /** Start/stop the simulated host transport. While running, ppq advances each
     *  block so an LSDj SYNC=MIDI role emits MIDI clock like a DAW would. */
    setTransport(running: boolean): void {
      client.setTransport(running);
    },
    /** Set the simulated host tempo in BPM (default 120). */
    setBpm(bpm: number): void {
      client.setBpm(bpm);
    },
    /** Take + clear the MIDI a role emitted back to the host since the last drain
     *  (e.g. Arduinoboy MI.OUT clock/notes). Call after runMs. */
    drainMidi(sys: number): MidiOutEvent[] {
      return client.drainMidi(sys);
    },
    /** Take + clear the raw GB serial-out bytes captured since the last drain
     *  (Arduinoboy master mode). Call after runMs. */
    drainSerial(sys: number): SerialOutByte[] {
      return client.drainSerial(sys);
    },
    /** Read a whole memory region as a copy (never a live view). */
    readMemory(sys: number, type: number): Uint8Array {
      return copyU8(client.readMemory(sys, type));
    },
    /** Name-keyed CPU register file (throws if the system has no CPU state). */
    getRegisters(sys: number): CpuRegisters {
      const out: CpuRegisters = {};
      for (const r of client.getRegisters(sys)) out[r.name] = r.value;
      return out;
    },
    /** Write one CPU register by name, e.g. setRegister(sys, "pc", 0x150). */
    setRegister(sys: number, name: string, value: number): void {
      client.setRegister(sys, name, value);
    },
    /** Side-effect-free read of one CPU address-space byte (throws if the
     *  backend doesn't support it — use readMemory regions instead). */
    readCpu(sys: number, addr: number): number {
      return client.readCpu(sys, addr);
    },
    /** Advance one instruction; returns cycles run (0 = backend can't step). */
    step(sys: number): number {
      return client.step(sys);
    },
    /** Run until PC === target or maxCycles elapse. False if hit cap / can't step. */
    runUntilPc(sys: number, pc: number, maxCycles: number): boolean {
      return client.runUntilPc(sys, pc, maxCycles);
    },
    /** Start profiling: init the debugger + reset the profiler. Mesen NES only.
     *  Drive execution with runMs between this and readProfile. */
    beginProfile(sys: number): void {
      client.beginProfile(sys);
    },
    /** Read accumulated profiler stats, sorted by exclusive cycles (hottest first). */
    readProfile(sys: number): ProfiledFunction[] {
      return client.readProfile(sys);
    },
    /** Load a cc65 .dbg so profiler/disasm/callstack output shows symbol names. */
    loadLabels(sys: number, path: string): boolean {
      return client.loadLabels(sys, path);
    },
    /** Disassemble `count` instructions from CPU address `addr`. */
    disassemble(sys: number, addr: number, count: number): DisasmLine[] {
      return client.disassemble(sys, addr, count);
    },
    /** Enable/disable the execution trace logger (enable before the run window). */
    setTrace(sys: number, on: boolean): void {
      client.setTrace(sys, on);
    },
    /** Most recent `count` executed instructions (row 0 = most recent). */
    readTrace(sys: number, count: number): TraceLine[] {
      return client.readTrace(sys, count);
    },
    /** Current call stack (outermost first), each frame named when labels load. */
    getCallStack(sys: number): CallFrame[] {
      return client.getCallStack(sys);
    },
    /** Snapshot the NES APU's five channels' decoded state — per channel:
     *  period/duty/volume/enable/output (+ sweep, envelope, length, DMC sample
     *  fields). Mesen NES only (throws on other backends). The raw APU registers
     *  are write-only on the NES, so this is how a MIDI-driven ROM's effect on
     *  the sound chip is observed; call after runMs. */
    getApuState(sys: number): ApuState {
      return client.getApuState(sys) as unknown as ApuState;
    },
    /** Install breakpoints/watchpoints (replaces existing; [] clears). Drive with
     *  runUntilBreak, not runMs, while breakpoints are active. */
    setBreakpoints(sys: number, bps: BreakpointSpec[]): void {
      client.setBreakpoints(sys, bps.map((b) => ({
        type: b.type, start: b.start, end: b.end ?? 0, condition: b.condition ?? "",
      })));
    },
    /** Run until a breakpoint fires or maxCycles elapse. */
    runUntilBreak(sys: number, maxCycles: number): BreakInfo {
      return client.runUntilBreak(sys, maxCycles);
    },
    /** Single-step into the next instruction. */
    stepInto(sys: number): BreakInfo {
      return client.stepInto(sys);
    },
    /** Single-step, executing a subroutine call as one step. */
    stepOver(sys: number): BreakInfo {
      return client.stepOver(sys);
    },
    /** Run until the current subroutine returns. */
    stepOut(sys: number): BreakInfo {
      return client.stepOut(sys);
    },
    /** Current framebuffer: {width,height,published, pixels:Uint8Array XRGB8888}. */
    getFrame(sys: number): Frame {
      const f = client.getFrame(sys);
      return { width: f.width, height: f.height, published: f.published,
               pixels: copyU8(f.data as unknown) };
    },
    /** Write the current framebuffer to a PNG; false if no frame yet. */
    screenshot(sys: number, path: string): boolean {
      return client.screenshot(sys, path);
    },
    /** Advance `ms` and return the mixed stereo output, interleaved L,R,L,R…. */
    getAudio(ms: number): Float32Array {
      return toFloat32(client.getAudio(ms));
    },
    /** Advance `ms` and return each system's audio in its own interleaved stereo
     *  buffer (result[i] = system i). SameBoy-only. Use to prove LSDj link-cable
     *  sync: the follower produces audio only when actually synced to the leader. */
    runMsPerSystem(ms: number): Float32Array[] {
      return (client.runMsPerSystem(ms).systems as unknown as Uint8Array[]).map(toFloat32);
    },
    /** Write interleaved stereo float32 audio to a 16-bit WAV (for external
     *  inspection, e.g. the reaper MCP audio-analysis workflow). */
    writeWav(path: string, samples: Float32Array, sampleRate = 44100): void {
      client.writeWav(path,
        toNums(new Uint8Array(samples.buffer, samples.byteOffset, samples.byteLength)),
        sampleRate);
    },
    /** Stream `ms` of the mixed stereo render straight to a WAV file, block by
     *  block — a long render never buffers the whole song in JS (the getAudio +
     *  writeWav path would). sampleRate is the WAV header rate. */
    renderWav(path: string, ms: number, sampleRate = 44100): void {
      client.renderWav(path, ms, sampleRate);
    },
    /** Stream a per-system render in one pass: each system's stereo output to its
     *  own path in `perSystemPaths` (one per loaded system, in load order), and —
     *  when `mixPath` is non-empty — their sum to the mix WAV. SameBoy-only. */
    renderWavPerSystem(mixPath: string, perSystemPaths: string[], ms: number,
                       sampleRate = 44100): void {
      client.renderWavPerSystem(mixPath, perSystemPaths, ms, sampleRate);
    },
    /** Like renderWavPerSystem but renders each unit (a system, or a SameBoy
     *  link group) on its own worker thread, then sums the mix single-threaded —
     *  byte-identical audio, faster on multi-unit projects, and not SameBoy-only
     *  (covers Mesen too). In-RAM (same profile as runMsPerSystem). Pure audio:
     *  role MIDI/serial output is discarded, not captured. */
    renderWavPerSystemParallel(mixPath: string, perSystemPaths: string[], ms: number,
                               sampleRate = 44100): void {
      client.renderWavPerSystemParallel(mixPath, perSystemPaths, ms, sampleRate);
    },
    /** Open a render session: subsequent renderChunk(ms) calls append a
     *  contiguous WAV while you apply scripted input between them, then
     *  renderEnd() closes the writers. `mixPath` "" = no mix; `perSystemPaths`
     *  non-empty = per-system mode (one path per loaded system, SameBoy-only). */
    renderBegin(mixPath: string, perSystemPaths: string[], sampleRate = 44100): void {
      client.renderBegin(mixPath, perSystemPaths, sampleRate);
    },
    /** Render `ms` into the open render session (advances the emulator). */
    renderChunk(ms: number): void {
      client.renderChunk(ms);
    },
    /** Close the render session, flushing the WAV writers. */
    renderEnd(): void {
      client.renderEnd();
    },
    /** Snapshot the project (config + savestate) to a .rplg fixture — the file a
     *  Reaper DAW test auto-loads via RETROPLUG_AUTOLOAD_PROJECT. */
    saveRplg(path: string): void {
      client.saveRplg(path);
    },
    /** Path-only JSON project save — the harness mirror of the plugin's default
     *  "Save Project" (config + romPath, no embedded binaries). A subsequent
     *  loadRplg re-reads the ROM from disk and the sibling `<rom>.sav`. Use
     *  saveRplg for the self-contained zip bundle. */
    saveProjectFile(path: string): void {
      client.saveProjectFile(path);
    },
    /** Inverse of saveRplg / saveProjectFile: rebuild the project from a project
     *  file (autodetecting zip vs path-only JSON), exactly as the plugin does on
     *  load (RETROPLUG_AUTOLOAD_PROJECT / setState). Replaces all current systems;
     *  returns the first restored system id. Use to round-trip a fixture and
     *  reproduce what a DAW sees on reload (e.g. whether a savestate restores to a
     *  playable state). */
    loadRplg(path: string): number {
      return client.loadRplg(path);
    },
    /** Compile a custom LSDj drum kit from sample files and queue it into `slot`.
     *  The sniffer auto-attaches the kit-patch role to LSDj ROMs; call runMs after
     *  so the role writes the bank into the cartridge ROM. */
    patchKit(sys: number, slot: number, name: string, samples: KitSample[]): void {
      client.patchKit(sys, slot, name, samples);
    },
    /**
     * A two-or-more-key chord (e.g. SELECT+UP). The modifier(s) lead the final
     * key by `staggerMs` and are released in reverse — the timing LSDJ needs.
     */
    chord(sys: number, buttons: number[], opts: ChordOpts = {}): void {
      const stagger = opts.staggerMs ?? 200;
      const hold = opts.holdMs ?? 200;
      const mods = buttons.slice(0, -1);
      const key = buttons[buttons.length - 1];
      for (const m of mods) emu.press(sys, m, true);
      emu.runMs(stagger);
      emu.press(sys, key, true);
      emu.runMs(hold);
      emu.press(sys, key, false);
      for (const m of [...mods].reverse()) emu.press(sys, m, false);
      emu.runMs(stagger);
    },
    /** A single button tap. Default hold is below LSDJ's auto-repeat threshold. */
    tap(sys: number, button: number, holdMs = 50): void {
      emu.press(sys, button, true);
      emu.runMs(holdMs);
      emu.press(sys, button, false);
      emu.runMs(50);
    },
  };

  return emu;
}

/** The control surface returned by createEmu. */
export type Emu = ReturnType<typeof createEmu>;
