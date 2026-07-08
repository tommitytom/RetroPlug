// openPath — the TS side of the editor's native "reveal in the OS file manager" seam (__rp_openPath).
// Used by Settings -> Open Settings Folder. Optional-chains the global so it is inert in the headless
// harness / a DAW (which never install it), exactly like __rp_setWindowSize / __rp_quitWindow.

/** Open `path` (a folder or file) in the OS file manager. No-op where the native seam isn't installed. */
export function openPath(path: string): void {
  if (path) (globalThis as { __rp_openPath?: (p: string) => void }).__rp_openPath?.(path);
}
