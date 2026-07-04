// In-memory Backend double for greenfield tests. No disk, no native: a Map of
// canonical-path -> bytes, a fixed config dir, and a lexical canonicalizer.
// Deterministic and inspectable — seed files, read them back, list what's on
// "disk". This is what lets the whole application layer be tested with `tjs run`
// and nothing else.

import type { Backend } from "../src/backend";

const enc = new TextEncoder();
const dec = new TextDecoder();

export class MockBackend implements Backend {
  private files = new Map<string, Uint8Array>();
  private dir: string;

  /** Names of Backend methods called, in order — lets tests assert side-effects
   *  (e.g. that a write went through writeFileAtomic, not writeFile). */
  readonly log: string[] = [];

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
}
