// In-memory Backend double for greenfield tests. No disk, no native: a Map of
// canonical-path -> bytes, a fixed config dir, and a lexical canonicalizer.
// Deterministic and inspectable — seed files, read them back, list what's on
// "disk". This is what lets the whole application layer be tested with `tjs run`
// and nothing else.

import type { Backend, ConstructSpec } from "../src/backend";
import { detectRomFormat } from "../src/romFormat";

const enc = new TextEncoder();
const dec = new TextDecoder();

// A constructed emulator, as the mock tracks it — enough to answer duplicate /
// reload / remove and to let tests inspect what native was asked to build.
interface MockSystem {
  romPath: string;
  embeddedRom: string;
  savPath: string | null;
  statePath: string | null;
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

  constructor(configDir = "/config") {
    this.dir = configDir;
  }

  // --- test helpers (not part of Backend) ---------------------------------

  /** Put a file on the fake disk (string is UTF-8 encoded). */
  seed(path: string, contents: string | Uint8Array): void {
    const bytes = typeof contents === "string" ? enc.encode(contents) : new Uint8Array(contents);
    this.files.set(this.canonicalize(path), bytes);
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

  removeSystem(id: number): boolean {
    this.log.push("removeSystem");
    return this.systems.delete(id);
  }

  // --- test helpers (not part of Backend) ---------------------------------

  /** The ids the mock currently considers live, sorted. */
  liveSystemIds(): number[] {
    return [...this.systems.keys()].sort((a, b) => a - b);
  }
}
