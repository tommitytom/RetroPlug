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

  /** The user configuration directory (where recent.json / config.json live),
   *  resolved per-OS (XDG / AppData / Application Support) with the
   *  RETROPLUG_USER_CONFIG_DIR override honoured. */
  configDir(): string;
}
