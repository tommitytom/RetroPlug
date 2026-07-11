// The single native-backend contract the greenfield TS application calls into.
//
// Everything the app cannot do in pure TS lives behind this one interface:
// filesystem bytes, atomic writes, path canonicalization, the OS config dir —
// and, as features land, file dialogs, emulator construction, the audio-thread
// command sink, and live emulator reads. The application code depends only on
// `Backend`; it never touches the real RPC bridge directly.
//
// It is SYNCHRONOUS on purpose. In the real plugin these are in-process calls
// over the txiki<->C++ bridge (createSyncClient on Symbol.for("plugin")), so
// modelling them as synchronous keeps the application logic straight-line and
// trivially testable. Tests inject an in-memory mock (testing/mockBackend.ts);
// the real adapter that forwards to the RPC bridge is wired in last, once the
// logic is proven.
//
// Grow this deliberately: add a method only when a feature genuinely needs the
// OS/emulator to do something TS can't. If a thing can be done in pure TS, it
// belongs in the application layer, not here.

import type { Platform, Core } from "./platform";

export interface Backend {
  // --- Filesystem ---------------------------------------------------------

  /** Read a file's bytes, or `null` when it is absent or unreadable. */
  readFile(path: string): Uint8Array | null;

  /** Write bytes to `path`, truncating. Returns false on failure. */
  writeFile(path: string, bytes: Uint8Array): boolean;

  /** Write via a temp file + atomic rename, so a crash mid-write can never
   *  leave a half-written (corrupt) file in place. Returns false on failure. */
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;

  /** True when `path` exists on disk. */
  fileExists(path: string): boolean;

  /** Move/rename `from` to `to`. Returns false on failure. */
  rename(from: string, to: string): boolean;

  /** The entry names directly under `dir` (files + subdirectories, not recursive), or an
   *  empty list when `dir` is absent. Generic readdir — filtering (e.g. `.json` profiles)
   *  is the caller's job. */
  listDir(dir: string): string[];

  /** Delete the file at `path`. Returns false when it isn't present. */
  deleteFile(path: string): boolean;

  /** The watched-file paths that changed since the last drain (empty when nothing did).
   *  Native owns the watching — efsw over the config dir + `bindings/`, plus the per-ROM
   *  mtime poll — and collects the changed paths; TS pulls + reacts at idle (re-read
   *  config, refresh bindings, reload a system whose ROM changed). A pull-drain, so the
   *  sync Backend stays sync. */
  drainChangedPaths(): string[];

  // --- Paths (the OS-specific bits TS can't reproduce) --------------------

  /** Normalize + resolve a path the way the OS would (weakly_canonical):
   *  collapse `.`/`..`, resolve the existing prefix's symlinks, absolutize.
   *  Tolerates non-existent paths. This is the dedupe key for path-keyed lists
   *  (e.g. recent files), so it must be stable for the same file. */
  canonicalize(path: string): string;

  /** Read the first `length` bytes of `path` (fewer if the file is shorter), or
   *  `null` when it is absent/unreadable. Lets TS classify a ROM from its header
   *  (≤ 0x134 bytes) without marshalling whole multi-MB files across the bridge —
   *  the seam that keeps all ROM knowledge (classification + pairing) in TS. */
  readFilePrefix(path: string, length: number): Uint8Array | null;

  /** The user configuration directory (where recent.json / config.json live),
   *  resolved per-OS (XDG / AppData / Application Support) with the
   *  RETROPLUG_USER_CONFIG_DIR override honoured. */
  configDir(): string;

  /** The app version string (e.g. "0.6.2"), single-sourced from the native Version.hpp — shown in the
   *  menu title chrome. */
  version(): string;

  // --- Emulator lifecycle -------------------------------------------------
  //
  // The narrow native service: build / clone / reload / drop an emulator. It takes
  // CONCRETE, already-resolved paths — TS does every bit of derivation (suffix math,
  // sibling paths, the paired-sav override, classification, pairing) with the pure
  // kernels and hands native only finished paths. Native never sees a suffix, never
  // looks for a sibling, never classifies. No ROM/SRAM bytes cross for construction:
  // native slurps the paths it's given.

  /** Build + activate an emulator from a spec under the TS-allocated `id` (TS owns the id counter;
   *  native never mints one). Returns whether it BUILT — true on success, false on an I/O failure
   *  (unreadable ROM / unknown format / pool full). With `replaceId` the instance swaps that id in
   *  place; otherwise it is appended. The adopt is fire-and-forget, so there's no id to return. */
  constructSystem(spec: ConstructSpec, id: number): boolean;

  // duplicate + reload are TS orchestration over constructSystem-with-state (SystemsStore): the store
  // pulls the source's savestate/SRAM from the registry and builds a seeded/replaceId core — native has
  // no bespoke method for either.

  /** Drop system `id`. Returns false when it isn't present. */
  removeSystem(id: number): boolean;

  // --- File dialog --------------------------------------------------------

  /** Open an OS file browser; resolves to the picked absolute path, or `null` on
   *  cancel. The ONE intrinsically-async Backend method: it waits on human input
   *  over DPF's non-blocking browser (there is no modal/synchronous variant), unlike
   *  the fast in-process calls above which stay synchronous. Modelling it as a Promise
   *  collapses the pending-mode latch + the 2nd-ROM browser into plain `await`s. */
  openFileBrowser(opts: FileBrowserOpts): Promise<string | null>;

  // --- Live emulator config ----------------------------------------------
  // TS owns the config; these apply a change to the LIVE emulator. Only EMULATOR
  // config crosses: the two universal settings + SYSTEM-role config (a backend's own
  // knobs). Feature-role config never reaches native — its behaviour is the deferred
  // TS-script future (doc 06), so it stays pure TS config.

  /** Apply a universal per-system setting (`"gainDb"` / `"reloadOnRomChange"`) to the
   *  live emulator. Returns false on failure. */
  applySystemSetting(id: number, key: string, value: number | boolean): boolean;

  /** Apply a SYSTEM-role's (backend) config to the live emulator — native dispatches
   *  by kind (e.g. `"sameboy"`: model → restart, highpass → live). Returns false on
   *  failure. */
  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;

  /** Arm/disarm capture of a system's outgoing serial bytes (LSDj MI.OUT). The store calls this when a
   *  system's `lsdj-sync` mode is (or leaves) MIDIOUT — armed, native feeds the raw bytes to the DSP
   *  kernel's `ctx.serialOut` for the Arduinoboy decoder. SameBoy-only (a no-op on other cores). */
  setSerialOutCapture(id: number, on: boolean): boolean;

  /** Set the project's audio-output routing (0 Stereo / 1 TwoPerInstance / 2 OnePerInstance) — which
   *  of the plugin's 4 stereo output pairs each system mixes into. The one project-level setting that
   *  reaches native audio (drives the block runner's MultiOutRouter). Returns false on an out-of-range
   *  mode. */
  setAudioRouting(mode: number): boolean;

  // --- Live emulator input ------------------------------------------------
  // The UI→DSP direction for game input: a joypad button transition on the focused system. Native queues
  // it to the core's audio-thread button sink, so it applies live while the instance plays.

  /** Press (`down`) or release a Game Boy button on system `id`. `button` is a GameboyButton value
   *  (Right=0 … Start=7). Effectively fire-and-forget — the edge is queued to the audio thread, so the
   *  return only says the call was accepted, not that the id exists. */
  pressButton(id: number, button: number, down: boolean): boolean;

  // --- Live emulator reads (the pump) -------------------------------------
  // The DSP→TS direction: read a live system's state out. Native publishes these from
  // the audio thread into race-free triple-buffers; TS pulls the latest snapshot by the
  // same id handle the store owns. This is how TS gathers the blobs it puts in an export
  // zip (the emulator state "exactly as it does today"); native only reads the bytes.

  /** The full savestate snapshot for system `id` (includes SRAM), or `null` when the
   *  handle is gone / nothing has been published yet. */
  readState(id: number): Uint8Array | null;

  /** The battery-backed SRAM region for system `id`, or `null`. */
  readSram(id: number): Uint8Array | null;

  /** The system's latest video frame for display, or `null` when the id is gone / has no framebuffer.
   *  `published` is false (and `pixels` empty) until the core has rendered a frame. Read from the
   *  race-free framebuffer triple-buffer, so it is safe to poll while the core plays. */
  getFrame(id: number): FrameData | null;

  // --- Live-core debug reads (spec/09-cli-debugging.md) -------------------
  // Unlike the snapshot-registry reads above, these read the LIVE core, so they are valid only on the
  // control thread while the audio thread is not started — the CLI's single-threaded direct-render
  // regime (a ROM-test harness never startAudio). NES-only today (empty on SameBoy/GBA).

  /** Decoded per-channel APU state for system `id`. Zeroed when the id is gone or the core has no
   *  APU debug target (SameBoy/GBA). Gate "is a note sounding" on `period > 0 && envelopeVolume > 0`,
   *  not the `enabled` ($4015) bit. */
  getApuState(id: number): ApuState;

  /** The NES PPU's live timing + register state (scanline/cycle/frameCount + the $2000/$2001/$2002
   *  register bytes + scroll). Zeroed when the id is gone or the core has no PPU debug target
   *  (SameBoy/GBA). The tilemap/sprite/palette viewers are not exposed. */
  getPpuState(id: number): PpuState;

  /** One CPU-visible byte at `addr` (side-effect-free peek — sees mapped I/O like a MIDI FIFO), or
   *  `null` when the id is gone or the peek is unsupported. */
  readCpu(id: number, addr: number): number | null;

  /** Debugger-style write of one byte at `addr` (the write counterpart of `readCpu`); returns true when
   *  the write was served, false when the id is gone or the backend can't write. NES-only today. */
  writeCpu(id: number, addr: number, value: number): boolean;

  /** A whole memory region's bytes (`MemoryRegion.*`), empty when the id/region is unsupported. */
  readMemory(id: number, region: number): Uint8Array | null;

  /** The system's CPU register file (name-keyed; every backend that supports it includes a "pc"). */
  getCpuRegisters(id: number): CpuRegister[];

  /** Single-step the core one instruction; returns the cycle count consumed (0 when unsupported). */
  stepInstruction(id: number): number;

  /** Drain the register/DMA/NMI/IRQ events Mesen's event viewer logged for the frame just rendered
   *  (write $4000-$4013 APU regs, $2000-$2007 PPU regs, etc). Frame-scoped — call after advancing the
   *  core. Empty when the id is gone or the core has no NES event viewer (SameBoy/GBA). */
  drainEvents(id: number): DebugEvent[];

  /** Load a cc65 `.dbg` symbol file (by path) so profiler/disassembly output shows function names.
   *  Returns false when the id is gone, the core has no NES debug target (SameBoy/GBA), or the file
   *  can't be read/parsed. */
  loadLabels(id: number, path: string): boolean;

  /** Write one CPU register by name (canonical lower-case, e.g. "a"/"pc"). Returns false when the id is
   *  gone, the backend can't write, or the name is unknown / read-only. */
  setCpuRegister(id: number, name: string, value: number): boolean;

  /** Single-step until PC == `target` or `maxCycles` elapse; true iff the target was reached (false when
   *  the id is gone, the backend can't step, or the cap trips first). */
  runUntilPc(id: number, target: number, maxCycles: number): boolean;

  /** Install the whole breakpoint set at once (replacing any prior set). Each `Breakpoint` is an
   *  execute/read/write watch over `[start,end]` with an optional Mesen condition expression. Returns
   *  false when the id is gone or the core has no NES debug target (SameBoy/GBA). */
  setBreakpoints(id: number, breakpoints: Breakpoint[]): boolean;

  /** Step the core until an installed breakpoint fires or `maxCycles` elapse. Returns the triggering
   *  BreakInfo (`broke` true + the hit `pc`/`breakpointId`), or broke=false + defaults on the cycle cap
   *  or when the id is gone / there is no NES debug target. */
  runUntilBreak(id: number, maxCycles: number): BreakInfo;

  /** Toggle Mesen's per-instruction execution trace logger. Returns false when the id is gone or the
   *  core has no NES debug target (SameBoy/GBA); read the captured rows afterwards with readTrace. */
  setTrace(id: number, on: boolean): boolean;

  /** The up-to-`count` most-recent trace rows (each a pc + formatted disassembly line), most-recent
   *  first. Empty when tracing was never enabled, the id is gone, or there is no NES debug target. */
  readTrace(id: number, count: number): TraceLine[];

  /** Single-step into the next instruction; returns the resulting BreakInfo (broke=false + defaults
   *  when the id is gone or the core can't step). */
  stepInto(id: number): BreakInfo;

  /** Single-step over a subroutine call (runs to the return); returns the resulting BreakInfo. */
  stepOver(id: number): BreakInfo;

  /** Run out of the current subroutine frame; returns the resulting BreakInfo. */
  stepOut(id: number): BreakInfo;

  /** Init the debugger + clear the profiler. Drive execution (a render window) between this and
   *  readProfile. Returns false when the id is gone or the core has no NES debug target (SameBoy/GBA). */
  beginProfile(id: number): boolean;

  /** The accumulated per-function profiler stats, hottest-first (descending `exclusiveCycles`). Empty
   *  when profiling was never begun, the id is gone, or there is no NES debug target. */
  readProfile(id: number): ProfiledFunction[];

  /** Disassemble `count` instructions from CPU address `addr`. Empty when the id is gone or the core has
   *  no NES debug target (SameBoy/GBA). */
  disassemble(id: number, addr: number, count: number): DisasmLine[];

  /** The current call stack (outermost first), each frame's entered address + resolved label. Empty when
   *  the id is gone, the stack is empty, or there is no NES debug target. */
  getCallStack(id: number): CallFrame[];

  // --- Byte codec ---------------------------------------------------------
  // The ONLY native part of `.rplg` export framing: TS assembles every entry (the thin
  // project.json + the per-system blobs) and hands them here to compress; native just
  // deflates (miniz). The inverse inflates a picked archive back to entries.

  /** Deflate `entries` into a PKZIP archive (`PK\x03\x04` magic), or `null` on failure. */
  zip(entries: ZipEntry[]): Uint8Array | null;

  /** Inflate a PKZIP archive back to its entries, or `null` when it isn't a valid zip. */
  unzip(bytes: Uint8Array): ZipEntry[] | null;

  // --- LSDj sav codec -----------------------------------------------------

  /** Encode an LSDj `.sav` image from a JSON `Sav` model (lenient — unset cells default). Backed by
   *  the pure-TS codec (src/lsdj); kept on the Backend seam so the load-time seed stays one call.
   *  `savFromJson("{}")` yields a valid, self-test-skipping 128 KiB image (the codec always stamps the
   *  `jk` + `rb` validity markers), which is how a load-time role seeds a fresh LSDj ROM. */
  savFromJson(json: string): Uint8Array;
}

/** One named blob in a zip archive (an `.rplg` entry: `project.json` or a
 *  `systems/{i}/…` blob). Bytes cross as `Uint8Array`, never a JS string. */
export interface ZipEntry {
  name: string;
  bytes: Uint8Array;
}

/** One system's video frame for display: raw XRGB8888 pixels (`width*height*4` bytes — the LVGL
 *  Canvas's native format). `published` is false, and `pixels` empty, until the core has rendered a
 *  frame. */
export interface FrameData {
  width: number;
  height: number;
  published: boolean;
  pixels: Uint8Array;
}

// --- Debug-read result types (mirror the native reflect-cpp structs in
// packages/native/src/system/DebugTarget.hpp + CpuState.hpp field-for-field, verbatim names — the RPC
// returns each as a plain JS object, so a direct cast is a faithful shape). ---

/** One 2A03 square (pulse) channel. `frequency` is the decoded Hz; `envelopeVolume`/`period` are the
 *  reliable "is it sounding" signal (`enabled` is only the $4015 switch). */
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

/** The NES PPU state snapshot (`getPpuState`) — verbatim mirror of the native `rp::PpuState`.
 *  `scanline` is -1..260 (-1 = pre-render), `cycle` 0..340, `frameCount` > 0 once the core has
 *  advanced. `control`/`mask`/`status` are the $2000/$2001/$2002 register bytes; `scrollX` the fine-X
 *  scroll; `videoRamAddr`/`tmpVideoRamAddr` the current/temp VRAM addresses (v/t); `writeToggle` the
 *  $2005/$2006 write latch (w); `spriteRamAddr` the $2003 OAM address. */
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

/** One named CPU register (`getCpuRegisters`). `value` is zero-extended to 32 bits; `bits` is the real
 *  width. NES (6502) reports `a/x/y/sp/ps` (8-bit) + `pc` (16-bit). */
export interface CpuRegister {
  name: string;
  value: number;
  bits: number;
}

/** One Mesen event-viewer event (`drainEvents`) — a register access / NMI / IRQ / DMA read logged for a
 *  frame. `type` is the DebugEventType ordinal (0=Register, 1=Nmi, 2=Irq, 3=Breakpoint, 4=BgColorChange,
 *  5=SpriteZeroHit, 6=DmcDmaRead, 7=DmaRead); `operationType` is the MemoryOperationType ordinal (0=Read,
 *  1=Write, ...). `address`/`value` are the register access (value -1 for a read with no captured value);
 *  `scanline`/`cycle` are the PPU position it fired at. */
export interface DebugEvent {
  type: number;
  operationType: number;
  address: number;
  value: number;
  programCounter: number;
  scanline: number;
  cycle: number;
}

/** One row of the execution trace (`readTrace`, most-recent first). `pc` is the instruction address;
 *  `text` is the formatted disassembly + register state Mesen logged for it. */
export interface TraceLine {
  pc: number;
  text: string;
}

/** Result of a step (`stepInto`/`stepOver`/`stepOut`) or `runUntilBreak`. `broke` is false when nothing
 *  fired (no debug target / cycle cap); `pc` is the triggering address (runUntilBreak) or the new PC
 *  (step); `breakpointId` is the hit breakpoint index, or -1 for a step / the cap. */
export interface BreakInfo {
  broke: boolean;
  pc: number;
  breakpointId: number;
}

/** A breakpoint to install via `setBreakpoints` (input; mirrors native `rp::BreakpointSpec`). `type` is
 *  "execute" (break when PC enters `[start,end]`) / "read" / "write" (break on a CPU access to the
 *  range). `end` defaults to `start` (single address); `condition` is an optional Mesen expression
 *  (e.g. "Y == 0"), empty for unconditional. */
export interface Breakpoint {
  type: "execute" | "read" | "write";
  start: number;
  end?: number;
  condition?: string;
}

/** One function's profiler sample (`readProfile`). `address` is the absolute ROM offset the profiler
 *  keys on; `label` is the resolved symbol name (empty until labels load). `exclusiveCycles` is time in
 *  the function itself (the bottleneck signal); `inclusiveCycles` counts callees too. */
export interface ProfiledFunction {
  address: number;
  label: string;
  exclusiveCycles: number;
  inclusiveCycles: number;
  callCount: number;
  minCycles: number;
  maxCycles: number;
}

/** One disassembled instruction (`disassemble`). `address` is the CPU address; `text` is the mnemonic +
 *  operands (symbol-resolved once labels load); `bytes` is the machine bytes as hex. */
export interface DisasmLine {
  address: number;
  text: string;
  bytes: string;
}

/** One call-stack frame (`getCallStack`, outermost first). `address` is the entered function's address;
 *  `label` is its resolved symbol (empty until labels load). */
export interface CallFrame {
  address: number;
  label: string;
}

/** Memory-region selector for `readMemory` — mirrors native `rp::MemoryType`. NES supports Ram/Rom/Sram/
 *  Vram/OAM/NametableRam; the rest are unsupported on NES (return empty). */
export const MemoryRegion = {
  Ram: 0,
  Rom: 1,
  Sram: 2,
  Vram: 3,
  IORegisters: 4,
  HRam: 5,
  OAM: 6,
  NametableRam: 7,
  ExtWorkRam: 8,
} as const;

/** Presentation for an OS file dialog: its title + the glob patterns to show. */
export interface FileBrowserOpts {
  title: string;
  patterns: string[];
  /** True for a SAVE dialog (a filename can be typed and an overwrite confirmed); absent/false
   *  opens for reading. Drives DPF's FileBrowserOptions.saving. */
  saving?: boolean;
  /** A suggested filename for a save dialog (e.g. `"project.rplg"`); ignored when opening. */
  defaultName?: string;
}

/** What TS hands the native builder: concrete paths only — everything is resolved
 *  TS-side before the call. `savPath`/`statePath` are the exact files to load-from +
 *  auto-save-to / boot-from, or `null` for fresh / cold. */
export interface ConstructSpec {
  /** The ROM file to slurp; `""` when an embedded ROM is used instead. */
  romPath: string;
  /** What the ROM targets (TS classifies it via platform.ts) — tells a multi-platform core (Mesen)
   *  which system to build. */
  platform: Platform;
  /** The emulator that runs it — the native factory's registry key ("sameboy" / "mesen"). Derived
   *  from `platform` via `defaultCoreFor` (no override in v1). */
  core: Core;
  /** A binary-baked ROM marker (e.g. `"mgb"`); `""` for a file-backed ROM. */
  embeddedRom: string;
  /** Exact battery file: SRAM is loaded from it and auto-saved to it. `null` = fresh. */
  savPath: string | null;
  /** Exact savestate file to boot from. `null` = cold boot (the systems domain
   *  always passes null; present for the later Project domain). */
  statePath: string | null;
  /** When set, swap this existing SystemId in place (load / replace); otherwise the
   *  new system is appended. */
  replaceId?: number;
  /** Seed SRAM bytes (a zip-import blob, a reload's carried battery, or a load-time role's
   *  synthesized sav). When set, native seeds from these instead of reading `savPath`; `savPath`
   *  remains the auto-save target. Rides the RPC bridge as a Uint8Array (rfl::Bytestring). */
  sramBytes?: Uint8Array;
  /** Seed savestate bytes (a zip-import blob or a duplicate's captured state). When set, native
   *  boots from these instead of reading `statePath`. Rides as a Uint8Array (rfl::Bytestring). */
  stateBytes?: Uint8Array;
  /** The system's backend ("system"-role) config as JSON, so a loaded non-default setting
   *  (SameBoy model/highpass/…) is applied at CONSTRUCT rather than via a post-build restart
   *  that would nuke the just-restored savestate. Omitted for a fresh build (backend defaults
   *  match the role-schema defaults). */
  settings?: string;
}
