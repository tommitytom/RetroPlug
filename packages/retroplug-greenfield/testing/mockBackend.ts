// In-memory Backend double for greenfield tests. No disk, no native: a Map of
// canonical-path -> bytes, a fixed config dir, and a lexical canonicalizer.
// Deterministic and inspectable — seed files, read them back, list what's on
// "disk". This is what lets the whole application layer be tested with `tjs run`
// and nothing else.

import type { Backend, ConstructSpec, FileBrowserOpts, ZipEntry } from "../src/backend";
import { detectRomFormat } from "../src/romFormat";

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
  private nextId = 1;
  private systems = new Map<number, MockSystem>();
  /** Every ConstructSpec passed to constructSystem, in order — lets tests assert
   *  the CONCRETE paths TS resolved (savPath/statePath/replaceId). */
  readonly constructCalls: ConstructSpec[] = [];
  /** (srcId, savPath) pairs passed to duplicateSystem, in order. */
  readonly duplicateCalls: { srcId: number; savPath: string | null }[] = [];

  // --- file-dialog bookkeeping (mock-only) --------------------------------
  /** Opts passed to each openFileBrowser call, in order — lets tests assert which
   *  dialog (ROM-or-sav vs ROM-only) was opened. */
  readonly fileBrowserCalls: FileBrowserOpts[] = [];
  /** One response per dialog the flow will open, consumed FIFO. `null` = cancel. */
  private browseQueue: (string | null)[] = [];

  /** Live-config applies recorded for assertions. */
  readonly applySettingCalls: { id: number; key: string; value: number | boolean }[] = [];
  readonly applyRoleCalls: { id: number; kind: string; config: Record<string, unknown> }[] = [];

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

  // --- Emulator lifecycle -------------------------------------------------

  constructSystem(spec: ConstructSpec): number | null {
    this.log.push("constructSystem");
    this.constructCalls.push(spec);
    // Build only when a real ROM is available: an embedded marker, or seeded ROM
    // bytes that classify as a known format (mirrors native's slurp + reject).
    if (!spec.embeddedRom) {
      const bytes = this.files.get(this.canonicalize(spec.romPath));
      if (!bytes || detectRomFormat(bytes) === "unknown") return null;
    }
    const id = this.nextId++;
    if (spec.replaceId !== undefined) this.systems.delete(spec.replaceId); // swap in place
    this.systems.set(id, {
      romPath: spec.romPath,
      embeddedRom: spec.embeddedRom,
      savPath: spec.savPath,
      statePath: spec.statePath,
      restoredFromBytes: spec.sramBytes !== undefined || spec.stateBytes !== undefined,
    });
    return id;
  }

  duplicateSystem(srcId: number, savPath: string | null): number | null {
    this.log.push("duplicateSystem");
    this.duplicateCalls.push({ srcId, savPath });
    const src = this.systems.get(srcId);
    if (!src) return null;
    const id = this.nextId++;
    this.systems.set(id, { ...src, savPath });
    return id;
  }

  reloadSystem(id: number): number | null {
    this.log.push("reloadSystem");
    const src = this.systems.get(id);
    if (!src) return null;
    const newId = this.nextId++;
    this.systems.delete(id);
    this.systems.set(newId, { ...src });
    return newId;
  }

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
