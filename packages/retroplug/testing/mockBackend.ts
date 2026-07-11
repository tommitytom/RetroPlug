// In-memory Backend double for greenfield tests. No disk, no native: a Map of
// canonical-path -> bytes, a fixed config dir, and a lexical canonicalizer.
// Deterministic and inspectable — seed files, read them back, list what's on
// "disk". This is what lets the whole application layer be tested with `tjs run`
// and nothing else.

import type { ApuState, ApuSquareState, Backend, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CpuRegister, DebugEvent, DisasmLine, FileBrowserOpts, FrameData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "../src/backend";
import { detectPlatform } from "../src/platform";
import { savFromJson } from "../src/lsdj";

const enc = new TextEncoder();
const dec = new TextDecoder();

// Local zip magic + a trivial, faithful-inverse entry encoding for the mock's
// zip/unzip (real native uses miniz DEFLATE; the mock just needs round-trip fidelity).
const PK_MAGIC = [0x50, 0x4b, 0x03, 0x04];
function pushU32(out: number[], n: number): void {
  out.push(n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff);
}
function readU32(b: Uint8Array, off: number): number {
  return (b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)) >>> 0;
}

// Deterministic per-id pump bytes, so a test can assert the exact blob that landed in
// the export zip. Tagged ("ST"/"SR") + id so state and sram never collide.
export function stateBytesFor(id: number): Uint8Array {
  return Uint8Array.of(0x53, 0x54, id & 0xff, (id >>> 8) & 0xff);
}
export function sramBytesFor(id: number): Uint8Array {
  return Uint8Array.of(0x53, 0x52, id & 0xff, (id >>> 8) & 0xff);
}

// A constructed emulator, as the mock tracks it — enough to answer duplicate /
// reload / remove and to let tests inspect what native was asked to build.
interface MockSystem {
  romPath: string;
  embeddedRom: string;
  savPath: string | null;
  statePath: string | null;
  /** True when the system was reconstructed from in-memory zip blobs (import). */
  restoredFromBytes: boolean;
}

export class MockBackend implements Backend {
  private files = new Map<string, Uint8Array>();
  private dir: string;

  /** Names of Backend methods called, in order — lets tests assert side-effects
   *  (e.g. that a write went through writeFileAtomic, not writeFile). */
  readonly log: string[] = [];

  // --- emulator-lifecycle bookkeeping (mock-only) -------------------------
  // (no id counter — TS owns ids and passes them to constructSystem.)
  private systems = new Map<number, MockSystem>();
  /** Every ConstructSpec passed to constructSystem, in order — lets tests assert
   *  the CONCRETE paths TS resolved (savPath/statePath/replaceId). */
  readonly constructCalls: ConstructSpec[] = [];
  // NOTE: duplicate/reload no longer hit the backend — they are TS orchestration over constructSystem,
  // so their effect shows up in constructCalls (with stateBytes/sramBytes + replaceId), not a dedicated log.

  // --- file-dialog bookkeeping (mock-only) --------------------------------
  /** Opts passed to each openFileBrowser call, in order — lets tests assert which
   *  dialog (ROM-or-sav vs ROM-only) was opened. */
  readonly fileBrowserCalls: FileBrowserOpts[] = [];
  /** One response per dialog the flow will open, consumed FIFO. `null` = cancel. */
  private browseQueue: (string | null)[] = [];

  /** Live-config applies recorded for assertions. */
  readonly applySettingCalls: { id: number; key: string; value: number | boolean }[] = [];
  readonly applyRoleCalls: { id: number; kind: string; config: Record<string, unknown> }[] = [];
  readonly serialOutCaptureCalls: { id: number; on: boolean }[] = [];
  readonly audioRoutingCalls: number[] = [];
  readonly pressButtonCalls: { id: number; button: number; down: boolean }[] = [];

  /** Ids passed to the pump reads, in order. */
  readonly readStateCalls: number[] = [];
  readonly readSramCalls: number[] = [];
  /** Test-driven SRAM content per system (setSram), overriding the deterministic
   *  default — lets a test model SRAM changing over time (dedup vs write). */
  private sramOverrides = new Map<number, Uint8Array>();

  /** Paths queued by emitFileChange, drained by drainChangedPaths — simulates the
   *  native watcher (efsw + ROM mtime poll) firing. */
  private changedPaths: string[] = [];
  /** Entry lists passed to zip / archives passed to unzip, in order. */
  readonly zipCalls: ZipEntry[][] = [];
  readonly unzipCalls: Uint8Array[] = [];

  constructor(configDir = "/config") {
    this.dir = configDir;
  }

  /** Seed the responses openFileBrowser will resolve to, in dialog order. */
  queueBrowse(...responses: (string | null)[]): void {
    this.browseQueue.push(...responses);
  }

  // --- test helpers (not part of Backend) ---------------------------------

  /** Put a file on the fake disk (string is UTF-8 encoded). */
  seed(path: string, contents: string | Uint8Array): void {
    const bytes = typeof contents === "string" ? enc.encode(contents) : new Uint8Array(contents);
    this.files.set(this.canonicalize(path), bytes);
  }

  /** Drive a system's live SRAM content (what readSram returns), overriding the
   *  deterministic default — lets a test model SRAM changing between flushes. */
  setSram(id: number, bytes: Uint8Array): void {
    this.sramOverrides.set(id, new Uint8Array(bytes));
  }

  /** Simulate the native watcher firing for `path` (config.json / a bindings profile /
   *  a ROM) — the next drainChangedPaths returns it. */
  emitFileChange(path: string): void {
    this.changedPaths.push(path);
  }

  /** Read a file back as text, or null if absent. */
  readText(path: string): string | null {
    const b = this.readFile(path);
    return b ? dec.decode(b) : null;
  }

  /** Every path currently on the fake disk, sorted. */
  paths(): string[] {
    return [...this.files.keys()].sort();
  }

  // --- Backend ------------------------------------------------------------

  readFile(path: string): Uint8Array | null {
    this.log.push("readFile");
    const b = this.files.get(this.canonicalize(path));
    return b ? new Uint8Array(b) : null;
  }

  writeFile(path: string, bytes: Uint8Array): boolean {
    this.log.push("writeFile");
    this.files.set(this.canonicalize(path), new Uint8Array(bytes));
    return true;
  }

  writeFileAtomic(path: string, bytes: Uint8Array): boolean {
    // Atomicity is invisible in-memory; behave like writeFile but log distinctly.
    this.log.push("writeFileAtomic");
    this.files.set(this.canonicalize(path), new Uint8Array(bytes));
    return true;
  }

  fileExists(path: string): boolean {
    this.log.push("fileExists");
    return this.files.has(this.canonicalize(path));
  }

  rename(from: string, to: string): boolean {
    this.log.push("rename");
    const cf = this.canonicalize(from);
    const bytes = this.files.get(cf);
    if (!bytes) return false;
    this.files.delete(cf);
    this.files.set(this.canonicalize(to), bytes);
    return true;
  }

  listDir(dir: string): string[] {
    this.log.push("listDir");
    const parent = this.canonicalize(dir);
    const out: string[] = [];
    for (const key of this.files.keys()) {
      const slash = key.lastIndexOf("/");
      const keyParent = slash <= 0 ? "/" : key.slice(0, slash);
      if (keyParent === parent) out.push(key.slice(slash + 1));
    }
    return out.sort();
  }

  deleteFile(path: string): boolean {
    this.log.push("deleteFile");
    return this.files.delete(this.canonicalize(path));
  }

  drainChangedPaths(): string[] {
    this.log.push("drainChangedPaths");
    const out = this.changedPaths;
    this.changedPaths = [];
    return out;
  }

  readFilePrefix(path: string, length: number): Uint8Array | null {
    this.log.push("readFilePrefix");
    const b = this.files.get(this.canonicalize(path));
    return b ? new Uint8Array(b.slice(0, length)) : null;
  }

  canonicalize(path: string): string {
    // Lexical normalize: absolutize against the config dir, collapse `.`/`..`
    // and duplicate separators. No symlink resolution (there's no real FS here),
    // which is faithful enough for tests — real symlink collapsing is the native
    // canonicalize's job.
    const abs = path.startsWith("/") ? path : `${this.dir}/${path}`;
    const out: string[] = [];
    for (const seg of abs.split(/[\\/]+/)) {
      if (seg === "" || seg === ".") continue;
      if (seg === "..") {
        if (out.length) out.pop();
      } else {
        out.push(seg);
      }
    }
    return "/" + out.join("/");
  }

  configDir(): string {
    return this.dir;
  }

  version(): string {
    return "0.0.0"; // placeholder; UI tests that assert a title set ctx.version explicitly
  }

  // --- Emulator lifecycle -------------------------------------------------

  constructSystem(spec: ConstructSpec, id: number): boolean {
    this.log.push("constructSystem");
    this.constructCalls.push(spec);
    // Build only when a real ROM is available: an embedded marker, or seeded ROM
    // bytes that classify as a known format (mirrors native's slurp + reject).
    if (!spec.embeddedRom) {
      const bytes = this.files.get(this.canonicalize(spec.romPath));
      if (!bytes || detectPlatform(bytes) === "unknown") return false;
    }
    if (spec.replaceId !== undefined) this.systems.delete(spec.replaceId); // swap in place
    this.systems.set(id, {  // TS owns the id counter; the mock just records under the given id
      romPath: spec.romPath,
      embeddedRom: spec.embeddedRom,
      savPath: spec.savPath,
      statePath: spec.statePath,
      restoredFromBytes: spec.sramBytes !== undefined || spec.stateBytes !== undefined,
    });
    return true;
  }

  // duplicate + reload are TS orchestration (SystemsStore) over readState/readSram + constructSystem —
  // the mock has no bespoke method; those store paths land as constructCalls (restoredFromBytes).

  readState(id: number): Uint8Array | null {
    this.log.push("readState");
    this.readStateCalls.push(id);
    return this.systems.has(id) ? stateBytesFor(id) : null;
  }

  readSram(id: number): Uint8Array | null {
    this.log.push("readSram");
    this.readSramCalls.push(id);
    const override = this.sramOverrides.get(id);
    if (override) return new Uint8Array(override);
    return this.systems.has(id) ? sramBytesFor(id) : null;
  }

  getFrame(id: number): FrameData | null {
    this.log.push("getFrame");
    // The mock never advances a real core, so a live system reports GB dimensions but no published
    // frame; an absent id is null. (Video rendering is proven against the native backend, not here.)
    if (!this.systems.has(id)) return null;
    return { width: 160, height: 144, published: false, pixels: new Uint8Array(0) };
  }

  // Live-core debug reads: the mock has no real core, so these are deterministic stand-ins that keep
  // `implements Backend` satisfied (the real behaviour is proven against the native backend).
  getApuState(_id: number): ApuState {
    this.log.push("getApuState");
    const square = (): ApuSquareState => ({
      enabled: false, period: 0, timer: 0, duty: 0, outputVolume: 0, frequency: 0, lengthCounter: 0,
      constantVolume: false, envelopeVolume: 0, sweepEnabled: false, sweepNegate: false, sweepPeriod: 0, sweepShift: 0,
    });
    return {
      pulse1: square(),
      pulse2: square(),
      triangle: { enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, linearCounter: 0 },
      noise: { enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, modeFlag: false, constantVolume: false, envelopeVolume: 0 },
      dmc: { enabled: false, sampleAddr: 0, sampleLength: 0, bytesRemaining: 0, period: 0, outputVolume: 0, loop: false, irqEnabled: false, sampleRate: 0 },
    };
  }

  getPpuState(_id: number): PpuState {
    this.log.push("getPpuState");
    return {
      scanline: 0, cycle: 0, frameCount: 0, control: 0, mask: 0, status: 0,
      scrollX: 0, videoRamAddr: 0, tmpVideoRamAddr: 0, writeToggle: false, spriteRamAddr: 0,
    };
  }

  readCpu(id: number, _addr: number): number | null {
    this.log.push("readCpu");
    return this.systems.has(id) ? 0 : null;
  }

  writeCpu(id: number, _addr: number, _value: number): boolean {
    this.log.push("writeCpu");
    return this.systems.has(id);
  }

  readMemory(id: number, _region: number): Uint8Array | null {
    this.log.push("readMemory");
    return this.systems.has(id) ? new Uint8Array(0) : null;
  }

  getCpuRegisters(_id: number): CpuRegister[] {
    this.log.push("getCpuRegisters");
    return [];
  }

  stepInstruction(id: number): number {
    this.log.push("stepInstruction");
    return this.systems.has(id) ? 1 : 0;
  }

  drainEvents(id: number): DebugEvent[] {
    this.log.push("drainEvents");
    // No real core: hand back one deterministic Register-write event when the id is live, otherwise none.
    return this.systems.has(id)
      ? [{ type: 0, operationType: 1, address: 0x4000, value: 0x80, programCounter: 0x8000, scanline: 0, cycle: 0 }]
      : [];
  }

  loadLabels(_id: number, _path: string): boolean {
    this.log.push("loadLabels");
    return false; // the mock has no NES debug target, so no symbol file is ever loaded
  }

  setCpuRegister(id: number, _name: string, _value: number): boolean {
    this.log.push("setCpuRegister");
    return this.systems.has(id);
  }

  runUntilPc(_id: number, _target: number, _maxCycles: number): boolean {
    this.log.push("runUntilPc");
    return false; // the mock has no real core to step, so a target PC is never reached
  }

  setBreakpoints(id: number, _breakpoints: Breakpoint[]): boolean {
    this.log.push("setBreakpoints");
    return this.systems.has(id); // installs nothing real; only a live NES core breaks
  }

  runUntilBreak(_id: number, _maxCycles: number): BreakInfo {
    this.log.push("runUntilBreak");
    return { broke: false, pc: 0, breakpointId: -1 }; // no real core → nothing ever fires
  }

  setTrace(id: number, _on: boolean): boolean {
    this.log.push("setTrace");
    return this.systems.has(id); // toggles nothing real; only a live NES core traces
  }

  readTrace(_id: number, _count: number): TraceLine[] {
    this.log.push("readTrace");
    return []; // the mock never captures a trace (no real core)
  }

  stepInto(id: number): BreakInfo {
    this.log.push("stepInto");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }

  stepOver(id: number): BreakInfo {
    this.log.push("stepOver");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }

  stepOut(id: number): BreakInfo {
    this.log.push("stepOut");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }

  beginProfile(id: number): boolean {
    this.log.push("beginProfile");
    return this.systems.has(id); // only a live NES core has a profiler
  }

  readProfile(_id: number): ProfiledFunction[] {
    this.log.push("readProfile");
    return []; // the mock never profiles (no real core)
  }

  disassemble(_id: number, _addr: number, _count: number): DisasmLine[] {
    this.log.push("disassemble");
    return []; // the mock has no disassembler (no real core)
  }

  getCallStack(_id: number): CallFrame[] {
    this.log.push("getCallStack");
    return []; // the mock has no call stack (no real core)
  }

  zip(entries: ZipEntry[]): Uint8Array | null {
    this.log.push("zip");
    this.zipCalls.push(entries.map((e) => ({ name: e.name, bytes: new Uint8Array(e.bytes) })));
    const parts: number[] = [...PK_MAGIC];
    for (const e of entries) {
      const name = enc.encode(e.name);
      pushU32(parts, name.length);
      for (const b of name) parts.push(b);
      pushU32(parts, e.bytes.length);
      for (const b of e.bytes) parts.push(b);
    }
    return new Uint8Array(parts);
  }

  unzip(bytes: Uint8Array): ZipEntry[] | null {
    this.log.push("unzip");
    this.unzipCalls.push(new Uint8Array(bytes));
    if (bytes.length < 4 || !PK_MAGIC.every((b, i) => bytes[i] === b)) return null;
    const out: ZipEntry[] = [];
    let off = 4;
    while (off + 4 <= bytes.length) {
      const nameLen = readU32(bytes, off);
      off += 4;
      const name = dec.decode(bytes.slice(off, off + nameLen));
      off += nameLen;
      const byteLen = readU32(bytes, off);
      off += 4;
      out.push({ name, bytes: bytes.slice(off, off + byteLen) });
      off += byteLen;
    }
    return out;
  }

  savFromJson(json: string): Uint8Array {
    this.log.push("savFromJson");
    // The LSDj codec is now pure TS, so the mock runs the REAL encoder — a valid
    // 128 KiB `jk`/`rb`-stamped image, no native host needed.
    return savFromJson(json);
  }

  removeSystem(id: number): boolean {
    this.log.push("removeSystem");
    return this.systems.delete(id);
  }

  openFileBrowser(opts: FileBrowserOpts): Promise<string | null> {
    this.log.push("openFileBrowser");
    this.fileBrowserCalls.push(opts);
    return Promise.resolve(this.browseQueue.length ? (this.browseQueue.shift() as string | null) : null);
  }

  applySystemSetting(id: number, key: string, value: number | boolean): boolean {
    this.log.push("applySystemSetting");
    this.applySettingCalls.push({ id, key, value });
    return true;
  }

  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
    this.log.push("applyRoleConfig");
    this.applyRoleCalls.push({ id, kind, config });
    return true;
  }

  setSerialOutCapture(id: number, on: boolean): boolean {
    this.log.push("setSerialOutCapture");
    this.serialOutCaptureCalls.push({ id, on });
    return true;
  }

  setAudioRouting(mode: number): boolean {
    this.log.push("setAudioRouting");
    this.audioRoutingCalls.push(mode);
    return mode >= 0 && mode <= 2;
  }

  pressButton(id: number, button: number, down: boolean): boolean {
    this.log.push("pressButton");
    this.pressButtonCalls.push({ id, button, down });
    return true;
  }

  // --- test helpers (not part of Backend) ---------------------------------

  /** The ids the mock currently considers live, sorted. */
  liveSystemIds(): number[] {
    return [...this.systems.keys()].sort((a, b) => a - b);
  }

  /** The live ids that were reconstructed from in-memory zip blobs (import), sorted. */
  restoredIds(): number[] {
    return [...this.systems.entries()].filter(([, s]) => s.restoredFromBytes).map(([id]) => id).sort((a, b) => a - b);
  }
}
