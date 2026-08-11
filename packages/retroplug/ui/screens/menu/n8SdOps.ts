// SD-card / menu operations against a physical Everdrive N8 Pro (Load ROM, Dump SRAM, Restore SRAM), for the
// N8 Pro submenu (in the instance menu's tracker block). Unlike the streaming config (n8Devices.ts), these are LONG blocking serial ops,
// so they run on a native background worker (N8SdWorker); this module only kicks them off through the
// __rp_n8* hooks (bound by bindN8Hooks, the same family as n8Devices.ts) and exposes a polled status
// snapshot. The worker pauses streaming, borrows the one serial port, and resumes (except after a ROM load,
// which boots a new game). Available in BOTH the SDL standalone and the DAW plugin; absent in the headless
// harness (hasN8Sd() then false -> the rows are hidden). The App polls getN8SdStatus() on the frame bus while
// a job runs (useN8SdWatch) so the progress row tracks it live.

export interface N8SdStatus {
  busy: boolean;
  op: string; // "load" | "dump" | "restore" | "" (idle)
  bytesDone: number;
  bytesTotal: number; // 0 = indeterminate
  phase: string;
  done: boolean; // the last job finished (success or error)
  error: string; // "" = ok
  result: string; // human summary on success
  version: number; // bumps on any change, so the UI re-renders only when it moves
}

type N8SdGlobals = {
  __rp_getN8SdStatus?: () => Partial<N8SdStatus>;
  __rp_n8LoadRom?: (path: string) => void;
  __rp_n8DumpSram?: (path: string) => void;
  __rp_n8RestoreSram?: (path: string) => void;
};

/** Whether the host exposes the N8 SD-op seam (bound alongside the streaming config in bindN8Hooks). */
export function hasN8Sd(): boolean {
  return typeof (globalThis as N8SdGlobals).__rp_getN8SdStatus === "function";
}

/** The live SD-job snapshot, read fresh each poll. null on a host without the seam. */
export function getN8SdStatus(): N8SdStatus | null {
  const fn = (globalThis as N8SdGlobals).__rp_getN8SdStatus;
  if (typeof fn !== "function") return null;
  const s = fn() ?? {};
  return {
    busy: !!s.busy,
    op: typeof s.op === "string" ? s.op : "",
    bytesDone: typeof s.bytesDone === "number" ? s.bytesDone : 0,
    bytesTotal: typeof s.bytesTotal === "number" ? s.bytesTotal : 0,
    phase: typeof s.phase === "string" ? s.phase : "",
    done: !!s.done,
    error: typeof s.error === "string" ? s.error : "",
    result: typeof s.result === "string" ? s.result : "",
    version: typeof s.version === "number" ? s.version : 0,
  };
}

/** Upload a local ROM to the cart SD and boot it (needs the cart at its menu). Fire-and-forget. */
export function n8LoadRom(path: string): void {
  (globalThis as N8SdGlobals).__rp_n8LoadRom?.(path);
}

/** Read the 64 KB cart battery and save it to a local file. Fire-and-forget. */
export function n8DumpSram(path: string): void {
  (globalThis as N8SdGlobals).__rp_n8DumpSram?.(path);
}

/** Write a local .srm straight to the running game's cart SRAM. Fire-and-forget. */
export function n8RestoreSram(path: string): void {
  (globalThis as N8SdGlobals).__rp_n8RestoreSram?.(path);
}
