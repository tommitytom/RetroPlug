// In-memory Backend double for tests. No disk, no native: a Map of
// canonical-path -> bytes, a fixed config dir, and a lexical canonicalizer.
// Deterministic and inspectable — seed files, read them back, list what's on
// "disk". This is what lets the whole application layer be tested with `tjs run`
// and nothing else.

import type { ApuState, ApuSquareState, Backend, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "../src/backend";
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
  /** Test-driven WRAM content per system (setRam) — lets a mock-tier test feed the LSDj runtime reader
   *  a synthetic WRAM snapshot. Absent → readRam returns null (the mock has no real core). */
  private ramOverrides = new Map<number, Uint8Array>();

  /** Paths queued by emitFileChange, drained by drainChangedPaths — simulates the
   *  native watcher (efsw) firing. */
  private changedPaths: string[] = [];
  /** The last ROM set passed to setWatchedRoms — for tests asserting the policy wiring. */
  watchedRoms: string[] = [];
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
  setRam(id: number, bytes: Uint8Array): void {
    this.ramOverrides.set(id, new Uint8Array(bytes));
  }

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

  appendFile(path: string, bytes: Uint8Array): boolean {
    this.log.push("appendFile");
    const key = this.canonicalize(path);
    const prev = this.files.get(key) ?? new Uint8Array(0);
    const out = new Uint8Array(prev.length + bytes.length);
    out.set(prev);
    out.set(bytes, prev.length);
    this.files.set(key, out);
    return true;
  }

  writeFileAt(path: string, offset: number, bytes: Uint8Array): boolean {
    this.log.push("writeFileAt");
    const key = this.canonicalize(path);
    const prev = this.files.get(key);
    if (!prev) return false; // mirrors native: the file must exist (fstream in|out doesn't create)
    const end = offset + bytes.length;
    const out = end > prev.length ? new Uint8Array(end) : new Uint8Array(prev.length);
    out.set(prev);
    out.set(bytes, offset);
    this.files.set(key, out);
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
    const prefix = parent === "/" ? "/" : parent + "/";
    // Immediate children: a direct file, or a subdirectory (marked with a trailing '/', matching the native
    // listDir contract) inferred from any deeper file path under `parent`.
    const names = new Set<string>();
    for (const key of this.files.keys()) {
      if (!key.startsWith(prefix)) continue;
      const rest = key.slice(prefix.length);
      const slash = rest.indexOf("/");
      names.add(slash < 0 ? rest : rest.slice(0, slash) + "/");
    }
    return [...names].sort();
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

  setWatchedRoms(paths: string[]): void {
    this.log.push("setWatchedRoms");
    this.watchedRoms = [...paths];
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
    // Build only when a real ROM is available: an embedded marker, TS-supplied effective ROM bytes
    // (romBytes wins, as native does), or the on-disk romPath — classified to a known format.
    //
    // The SMS/GG carve-out mirrors MesenBackend's gate, and has to: the Sega magic is optional, so
    // headerless homebrew reaches construct with bytes that classify as "unknown". Native accepts those
    // (rejecting only bytes that are positively another platform), and a stricter mock would fail every
    // headerless-ROM store test against a backend that would really have built it.
    if (!spec.embeddedRom) {
      const bytes = spec.romBytes ?? this.files.get(this.canonicalize(spec.romPath));
      if (!bytes) return false;
      const fmt = detectPlatform(bytes);
      const sega = spec.platform === "sms" || spec.platform === "gg";
      if (fmt === "unknown" ? !sega : sega && fmt !== "sms" && fmt !== "gg") return false;
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

  readRam(id: number): Uint8Array | null {
    this.log.push("readRam");
    const override = this.ramOverrides.get(id);
    if (override) return new Uint8Array(override);
    return null; // the mock has no real core → no WRAM unless a test set one
  }

  /** Mirrors the native contract: write into the SAME buffer readRam returns, refuse out-of-bounds, and
   *  refuse when there is no RAM at all (the mock has none until a test calls setRam). */
  writeRam(id: number, offset: number, bytes: Uint8Array): boolean {
    this.log.push("writeRam");
    const ram = this.ramOverrides.get(id);
    if (!ram || !bytes.length) return false;
    if (offset < 0 || offset + bytes.length > ram.length) return false;
    ram.set(bytes, offset);
    return true;
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
      constantVolume: false, envelopeVolume: 0, envelopeLevel: 0, envelopeOutput: 0, envelopeLoop: false,
      sweepEnabled: false, sweepNegate: false, sweepPeriod: 0, sweepShift: 0,
    });
    return {
      pulse1: square(),
      pulse2: square(),
      triangle: { enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, linearCounter: 0 },
      noise: {
        enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, modeFlag: false,
        constantVolume: false, envelopeVolume: 0, envelopeLevel: 0, envelopeOutput: 0, envelopeLoop: false,
      },
      dmc: { enabled: false, sampleAddr: 0, sampleLength: 0, bytesRemaining: 0, period: 0, outputVolume: 0, loop: false, irqEnabled: false, sampleRate: 0 },
    };
  }

  getExpansionAudioState(_id: number): ExpansionAudioState {
    this.log.push("getExpansionAudioState");
    return { chip: "none", channels: [] };
  }

  getPpuState(_id: number): PpuState {
    this.log.push("getPpuState");
    return {
      scanline: 0, cycle: 0, frameCount: 0, control: 0, mask: 0, status: 0,
      scrollX: 0, videoRamAddr: 0, tmpVideoRamAddr: 0, writeToggle: false, spriteRamAddr: 0,
      paletteRam: new Uint8Array(32),
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
      ? [{ type: 0, operationType: 1, address: 0x4000, value: 0x80, programCounter: 0x8000, scanline: 0, cycle: 0, frame: 1 }]
      : [];
  }

  loadLabels(_id: number, _path: string): boolean {
    this.log.push("loadLabels");
    return false; // the mock has no NES debug target, so no symbol file is ever loaded
  }

  symbolAddress(_id: number, _name: string): number | null {
    this.log.push("symbolAddress");
    return null; // nothing is ever loaded (see loadLabels)
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

  // A trivial lossless RGBA container (magic "RPNG" + w + h + raw rgba) — NOT real PNG. Like the mock
  // zip/unzip, it just needs round-trip fidelity so pure tests can exercise the font tile↔rgba mapping;
  // decoding a real .png file is covered by the test-native suite (real lodepng host).
  pngEncode(width: number, height: number, rgba: Uint8Array): Uint8Array | null {
    this.log.push("pngEncode");
    if (rgba.length < width * height * 4) return null;
    const parts: number[] = [0x52, 0x50, 0x4e, 0x47]; // "RPNG"
    pushU32(parts, width);
    pushU32(parts, height);
    return new Uint8Array([...parts, ...rgba.subarray(0, width * height * 4)]);
  }

  pngDecode(bytes: Uint8Array): PngImageData | null {
    this.log.push("pngDecode");
    if (bytes.length < 12 || bytes[0] !== 0x52 || bytes[1] !== 0x50 || bytes[2] !== 0x4e || bytes[3] !== 0x47) return null;
    const width = readU32(bytes, 4);
    const height = readU32(bytes, 8);
    const rgba = bytes.slice(12, 12 + width * height * 4);
    if (rgba.length < width * height * 4) return null;
    return { width, height, rgba };
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
    return mode >= 0 && mode <= 3; // 0 Stereo / 1 TwoPerInstance / 2 OnePerInstance / 3 ChannelSplit
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
