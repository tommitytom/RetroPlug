// The standalone's local transport (Instance menu > Transport). Mirrors midiDevices.ts: the value lives in
// native, a pick applies immediately, and a subscribable forces the label to repaint rather than waiting for
// an unrelated re-render. See __rp_getTransport / __rp_setTransport in packages/native/sdl/main.cpp.
//
// Why this exists at all: a running transport is not idle state, it is a clock. The LSDj / risa sync roles
// turn `block.transport` into link-port bytes, so while it runs a SYNC=MIDI cart is being driven — which is
// why the standalone starting out "playing" (its behaviour until now) meant a cart began running at 120 BPM
// on boot with nothing plugged in. It now starts stopped and this row is how you start it.
//
// Absent in a DAW / the headless harness (the host owns the transport there) → hasTransport() false → the row
// is hidden, exactly like the audio/MIDI device rows.

export interface TransportState {
  playing: boolean; // what the Engine is being told right now
  external: boolean; // a MIDI clock master is driving it — our own settings are recorded but overridden
  bpm: number; // the tempo in force (the master's while `external`)
  localBpm: number; // our own tempo — what comes back when the master goes away
}

/** The tempo range the host accepts, matching the window an external clock's estimate is trusted in
 *  (MidiClockSync::kMinBpm/kMaxBpm). Steps: fine on Left/Right, coarse on PageUp/PageDown. */
export const CLOCK_BPM_MIN = 20;
export const CLOCK_BPM_MAX = 999;
export const CLOCK_BPM_STEP = 1;
export const CLOCK_BPM_COARSE_STEP = 10;

let version = 0;
const listeners = new Set<() => void>();
function emit(): void {
  version++;
  for (const l of listeners) l();
}

type TransportGlobals = {
  __rp_getTransport?: () => Partial<TransportState>;
  __rp_setTransport?: (playing: boolean) => void;
  __rp_setClockBpm?: (bpm: number) => void;
};

/** Whether the host exposes the transport seam (standalone only). Gates the row. */
export function hasTransport(): boolean {
  return typeof (globalThis as TransportGlobals).__rp_getTransport === "function";
}

/** The live transport, read fresh each render (the audio thread publishes it every block). */
export function getTransport(): TransportState | null {
  const fn = (globalThis as TransportGlobals).__rp_getTransport;
  if (typeof fn !== "function") return null;
  const t = fn() ?? {};
  const bpm = typeof t.bpm === "number" && t.bpm > 0 ? t.bpm : 120;
  return {
    playing: t.playing === true,
    external: t.external === true,
    bpm,
    localBpm: typeof t.localBpm === "number" && t.localBpm > 0 ? t.localBpm : bpm,
  };
}

/** Start/stop the local transport. Applies on the next audio block, then repaints the label. */
export function setTransport(playing: boolean): void {
  (globalThis as TransportGlobals).__rp_setTransport?.(playing);
  emit();
}

/** Set the local tempo (clamped here as well as natively, so the label can't show a value the host
 *  refused). Applies on the next audio block and persists; ignored while an external master is running. */
export function setClockBpm(bpm: number): void {
  const clamped = Math.min(CLOCK_BPM_MAX, Math.max(CLOCK_BPM_MIN, Math.round(bpm)));
  (globalThis as TransportGlobals).__rp_setClockBpm?.(clamped);
  emit();
}

/** A monotonic version — a stable snapshot for App's forced re-render on a transport change. */
export function transportVersion(): number {
  return version;
}

export function subscribeTransport(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
