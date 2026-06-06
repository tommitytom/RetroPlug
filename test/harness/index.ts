// retroplug-cli TypeScript test harness — the front door test files import:
//
//   import { test, expect, emu, Button, Mem } from "harness";
//
// Runs inside the embedded txiki/QuickJS runtime (see cli/TestHarness.cpp).
// `test()` registers cases; on the runtime's synthetic window 'load' event we
// run each one, turning any thrown error (from expect() or a native emu call)
// into a TAP `not ok`. All emulator control is synchronous — press, advance
// time, read memory, assert, in order.

// Native namespace bound by TestHarness.cpp on Symbol.for("retroplug").
interface NativeRp {
  // Optional `sav` (an ArrayBuffer of cartridge SRAM, e.g. from savFromJson)
  // boots the system from that .sav image.
  loadRom(path: string, sav?: ArrayBuffer): number;
  // Build a 128 KiB .sav image from a (possibly partial) Sav-model JSON fixture.
  savFromJson(json: string): ArrayBuffer;
  runMs(ms: number): void;
  press(sys: number, button: number, down: boolean): void;
  sendMidi(sys: number, bytes: number[]): void;
  readMemory(sys: number, type: number): ArrayBuffer;
  getFrame(sys: number): { width: number; height: number; published: boolean; data: ArrayBuffer };
  screenshot(sys: number, path: string): boolean;
  getAudio(ms: number): ArrayBuffer;
  getRegisters(sys: number): CpuRegisters;
  setRegister(sys: number, name: string, value: number): void;
  readCpu(sys: number, addr: number): number;
  step(sys: number): number;
  runUntilPc(sys: number, pc: number, maxCycles: number): boolean;
  beginProfile(sys: number): void;
  readProfile(sys: number): ProfiledFunction[];
  loadLabels(sys: number, path: string): boolean;
  disassemble(sys: number, addr: number, count: number): DisasmLine[];
  setTrace(sys: number, on: boolean): void;
  readTrace(sys: number, count: number): TraceLine[];
  getCallStack(sys: number): CallFrame[];
  setBreakpoints(sys: number, bps: BreakpointSpec[]): void;
  runUntilBreak(sys: number, maxCycles: number): BreakInfo;
  stepInto(sys: number): BreakInfo;
  stepOver(sys: number): BreakInfo;
  stepOut(sys: number): BreakInfo;
  beginCase(name: string): void;
  report(name: string, ok: boolean, message: string): void;
  done(): void;
  log(level: number, message: string): void;
}

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

/** Format profiler results as a hot-function table (for console.log). */
export function printProfile(fns: ProfiledFunction[], top = 20): string {
  const hex = (a: number) => "$" + (a >>> 0).toString(16).padStart(4, "0");
  const lines = fns.slice(0, top).map((f) =>
    `${String(f.exclusiveCycles).padStart(12)}  ${String(f.callCount).padStart(8)}  ` +
    `${f.label || hex(f.address)}`,
  );
  return [`${"exclCycles".padStart(12)}  ${"calls".padStart(8)}  function`, ...lines].join("\n");
}

const rp: NativeRp = (globalThis as any)[Symbol.for("retroplug")];

// -- Button / memory-region enums --------------------------------------------
//
// Hand-mirrored from src/system/InputTypes.hpp (GameboyButton) and
// src/system/MemoryType.hpp. The wire values are load-bearing; TestHarness.cpp
// holds matching static_assert guards so a C++ renumber breaks the build.

export const Button = {
  Right: 0, Left: 1, Up: 2, Down: 3, A: 4, B: 5, Select: 6, Start: 7,
} as const;

export const Mem = {
  Ram: 0, Rom: 1, Sram: 2, Vram: 3, IORegisters: 4,
  HRam: 5, OAM: 6, NametableRam: 7, ExtWorkRam: 8,
} as const;

export type ButtonId = (typeof Button)[keyof typeof Button];
export type MemType = (typeof Mem)[keyof typeof Mem];

// -- emu facade --------------------------------------------------------------

export interface ChordOpts { staggerMs?: number; holdMs?: number; }

export const emu = {
  /** Load a Game Boy ROM; returns the new system id. An optional `sav`
   *  ArrayBuffer (e.g. from savFromJson) boots the system from that .sav. */
  loadRom(path: string, sav?: ArrayBuffer): number {
    return rp.loadRom(path, sav);
  },
  /** Build a 128 KiB .sav image from a (possibly partial) Sav-model JSON
   *  fixture — missing fields take model defaults. Feed the result to
   *  loadRom(rom, sav) to boot LSDj from an authored song. */
  savFromJson(json: string): ArrayBuffer {
    return rp.savFromJson(json);
  },
  /** Advance every loaded system by `ms` of emulated time. */
  runMs(ms: number): void {
    rp.runMs(ms);
  },
  /** Set a single button's state on a system. */
  press(sys: number, button: number, down: boolean): void {
    rp.press(sys, button, down);
  },
  /** Deliver a MIDI message (1-4 bytes, e.g. [0x90, note, vel]) to a system —
   *  queued for the next runMs. On NES it feeds the N8 MIDI FIFO, so profiling
   *  with sendMidi exercises the ROM's note-handling path, not just the idle loop. */
  sendMidi(sys: number, bytes: number[]): void {
    rp.sendMidi(sys, bytes);
  },
  /** Read a whole memory region as a copy (never a live view). */
  readMemory(sys: number, type: number): Uint8Array {
    return new Uint8Array(rp.readMemory(sys, type));
  },
  /** Name-keyed CPU register file (throws if the system has no CPU state). */
  getRegisters(sys: number): CpuRegisters {
    return rp.getRegisters(sys);
  },
  /** Write one CPU register by name, e.g. setRegister(sys, "pc", 0x150). */
  setRegister(sys: number, name: string, value: number): void {
    rp.setRegister(sys, name, value);
  },
  /** Side-effect-free read of one CPU address-space byte (throws if the
   *  backend doesn't support it — use readMemory regions instead). */
  readCpu(sys: number, addr: number): number {
    return rp.readCpu(sys, addr);
  },
  /** Advance one instruction; returns cycles run (0 = backend can't step). */
  step(sys: number): number {
    return rp.step(sys);
  },
  /** Run until PC === target or maxCycles elapse. False if hit cap / can't step. */
  runUntilPc(sys: number, pc: number, maxCycles: number): boolean {
    return rp.runUntilPc(sys, pc, maxCycles);
  },
  /** Start profiling: init the debugger + reset the profiler. Mesen NES only.
   *  Drive execution with runMs between this and readProfile. */
  beginProfile(sys: number): void {
    rp.beginProfile(sys);
  },
  /** Read accumulated profiler stats, sorted by exclusive cycles (hottest first). */
  readProfile(sys: number): ProfiledFunction[] {
    return rp.readProfile(sys);
  },
  /** Load a cc65 .dbg so profiler/disasm/callstack output shows symbol names. */
  loadLabels(sys: number, path: string): boolean {
    return rp.loadLabels(sys, path);
  },
  /** Disassemble `count` instructions from CPU address `addr`. */
  disassemble(sys: number, addr: number, count: number): DisasmLine[] {
    return rp.disassemble(sys, addr, count);
  },
  /** Enable/disable the execution trace logger (enable before the run window). */
  setTrace(sys: number, on: boolean): void {
    rp.setTrace(sys, on);
  },
  /** Most recent `count` executed instructions (row 0 = most recent). */
  readTrace(sys: number, count: number): TraceLine[] {
    return rp.readTrace(sys, count);
  },
  /** Current call stack (outermost first), each frame named when labels load. */
  getCallStack(sys: number): CallFrame[] {
    return rp.getCallStack(sys);
  },
  /** Install breakpoints/watchpoints (replaces existing; [] clears). Drive with
   *  runUntilBreak, not runMs, while breakpoints are active. */
  setBreakpoints(sys: number, bps: BreakpointSpec[]): void {
    rp.setBreakpoints(sys, bps);
  },
  /** Run until a breakpoint fires or maxCycles elapse. */
  runUntilBreak(sys: number, maxCycles: number): BreakInfo {
    return rp.runUntilBreak(sys, maxCycles);
  },
  /** Single-step into the next instruction. */
  stepInto(sys: number): BreakInfo {
    return rp.stepInto(sys);
  },
  /** Single-step, executing a subroutine call as one step. */
  stepOver(sys: number): BreakInfo {
    return rp.stepOver(sys);
  },
  /** Run until the current subroutine returns. */
  stepOut(sys: number): BreakInfo {
    return rp.stepOut(sys);
  },
  /** Current framebuffer: {width,height,published, pixels:Uint8Array XRGB8888}. */
  getFrame(sys: number): Frame {
    const f = rp.getFrame(sys);
    return { width: f.width, height: f.height, published: f.published,
             pixels: new Uint8Array(f.data) };
  },
  /** Write the current framebuffer to a PNG; false if no frame yet. */
  screenshot(sys: number, path: string): boolean {
    return rp.screenshot(sys, path);
  },
  /** Advance `ms` and return the mixed stereo output, interleaved L,R,L,R…. */
  getAudio(ms: number): Float32Array {
    return new Float32Array(rp.getAudio(ms));
  },
  /**
   * A two-or-more-key chord (e.g. SELECT+UP). The modifier(s) lead the final
   * key by `staggerMs` and are released in reverse — the timing LSDJ needs.
   * Mirrors the JSON `chord` form (cli/Script.hpp).
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

export interface Frame {
  width: number;
  height: number;
  published: boolean;
  pixels: Uint8Array; // XRGB8888, width*height*4 bytes (empty if !published)
}

// -- test() / expect() -------------------------------------------------------

type TestFn = () => void;
const cases: { name: string; fn: TestFn }[] = [];

export function test(name: string, fn: TestFn): void {
  cases.push({ name, fn });
}

function fmt(v: unknown): string {
  if (v instanceof Uint8Array) return `Uint8Array(${v.length})`;
  try { return typeof v === "string" ? `"${v}"` : JSON.stringify(v); }
  catch { return String(v); }
}

function deepEqual(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (a instanceof Uint8Array && b instanceof Uint8Array) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    if (a.length !== b.length) return false;
    return a.every((x, i) => deepEqual(x, b[i]));
  }
  return false;
}

export function expect(actual: any) {
  return {
    toBe(expected: any) {
      if (actual !== expected)
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
    },
    toEqual(expected: any) {
      if (!deepEqual(actual, expected))
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
    },
    toBeGreaterThan(n: number) {
      if (!(actual > n)) throw new Error(`expected > ${n}, got ${fmt(actual)}`);
    },
    toBeLessThan(n: number) {
      if (!(actual < n)) throw new Error(`expected < ${n}, got ${fmt(actual)}`);
    },
    toBeTruthy() {
      if (!actual) throw new Error(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy() {
      if (actual) throw new Error(`expected falsy, got ${fmt(actual)}`);
    },
  };
}

// -- runner ------------------------------------------------------------------

function runAll(): void {
  for (const c of cases) {
    rp.beginCase(c.name);
    try {
      c.fn();
      rp.report(c.name, true, "");
    } catch (e: any) {
      rp.report(c.name, false, String((e && e.stack) || e));
    }
  }
  rp.done();
}

(globalThis as any).window.addEventListener("load", runAll);
