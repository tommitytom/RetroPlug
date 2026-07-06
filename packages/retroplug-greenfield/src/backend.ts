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

  // --- Emulator lifecycle -------------------------------------------------
  //
  // The narrow native service: build / clone / reload / drop an emulator. It takes
  // CONCRETE, already-resolved paths — TS does every bit of derivation (suffix math,
  // sibling paths, the paired-sav override, classification, pairing) with the pure
  // kernels and hands native only finished paths. Native never sees a suffix, never
  // looks for a sibling, never classifies. No ROM/SRAM bytes cross for construction:
  // native slurps the paths it's given.

  /** Build + activate an emulator from a spec, returning the new (monotonic)
   *  SystemId, or `null` on an I/O failure (unreadable ROM / unknown format). With
   *  `replaceId` the instance swaps that id in place; otherwise it is appended. */
  constructSystem(spec: ConstructSpec): number | null;

  /** Clone the LIVE state (savestate incl. SRAM) of system `srcId` into a fresh
   *  instance whose auto-save target is `savPath`. Returns the new id, or `null`. */
  duplicateSystem(srcId: number, savPath: string | null): number | null;

  /** Rebuild system `id`'s ROM from disk, carrying its live (unsaved) SRAM forward
   *  and keeping its paths, as a fresh instance swapped in place. Returns the new
   *  id, or `null`. (A live-emulator read only native can do.) */
  reloadSystem(id: number): number | null;

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

  // --- Byte codec ---------------------------------------------------------
  // The ONLY native part of `.rplg` export framing: TS assembles every entry (the thin
  // project.json + the per-system blobs) and hands them here to compress; native just
  // deflates (miniz). The inverse inflates a picked archive back to entries.

  /** Deflate `entries` into a PKZIP archive (`PK\x03\x04` magic), or `null` on failure. */
  zip(entries: ZipEntry[]): Uint8Array | null;

  /** Inflate a PKZIP archive back to its entries, or `null` when it isn't a valid zip. */
  unzip(bytes: Uint8Array): ZipEntry[] | null;
}

/** One named blob in a zip archive (an `.rplg` entry: `project.json` or a
 *  `systems/{i}/…` blob). Bytes cross as `Uint8Array`, never a JS string. */
export interface ZipEntry {
  name: string;
  bytes: Uint8Array;
}

/** Presentation for an OS file dialog: its title + the glob patterns to show. */
export interface FileBrowserOpts {
  title: string;
  patterns: string[];
}

/** What TS hands the native builder: concrete paths only — everything is resolved
 *  TS-side before the call. `savPath`/`statePath` are the exact files to load-from +
 *  auto-save-to / boot-from, or `null` for fresh / cold. */
export interface ConstructSpec {
  /** The ROM file to slurp; `""` when an embedded ROM is used instead. */
  romPath: string;
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
  /** Zip-import only: the initial SRAM bytes for a system restored from an export whose
   *  save lives in the archive (no on-disk path). When set, native seeds from these
   *  instead of reading `savPath`; `savPath` remains the auto-save target. */
  sramBytes?: ArrayBuffer;
  /** Zip-import only: the savestate bytes to boot from (from the archive). When set,
   *  native boots from these instead of reading `statePath`. */
  stateBytes?: ArrayBuffer;
  /** Optional LSDJ sync-role mode ("Off" / "MidiSync" / …). When set, native seeds the role
   *  at construct time (skipping the sniffer default); "Off" makes it emit no host clock. */
  lsdjSyncMode?: string;
}
