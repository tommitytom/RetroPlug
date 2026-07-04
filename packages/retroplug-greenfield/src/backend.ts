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
}
