// "Stream to a physical Everdrive N8 Pro" config for the N8 Pro submenu (in the instance menu's tracker
// block, beside risa/LSDj), available in BOTH the SDL
// standalone and the DAW plugin (bindN8Hooks binds the __rp_*N8* hooks in each host over its own N8Host).
// Mirrors midiDevices.ts: the state lives in the native host (the serial connection + lookahead + persisted
// n8.cfg), not a TS store, so this is a subscribable, not a store. Picks/toggles apply immediately (the host
// (re)connects the serial port + persists on the spot); App subscribes (subscribeN8) so the labels track the
// new value at once. The seam is present in both hosts and absent only in the headless harness (hasN8() then
// false -> submenu hidden). See __rp_getN8Config / __rp_setN8Port / __rp_connectN8 / __rp_setN8Lookahead, bound
// by bindN8Hooks (packages/native/src/host/n8/N8Hooks.cpp) from both sdl/main.cpp and plugin/PluginDSP.cpp.

export interface N8Port {
  port: string; // OS serial port name (/dev/ttyACM0, COM3, ...)
  isN8: boolean; // detected Everdrive N8 Pro (USB VID:PID 38df:0017)
}

export interface N8Config {
  ports: N8Port[]; // available serial ports
  selectedPort: string; // "" = none chosen (connect auto-picks the attached N8)
  connected: boolean; // the serial link is open + handshaken
  enabled: boolean; // the user's "stream to the N8" toggle
  lookaheadMs: number; // timed-release latency the serial thread applies
  bytes: number; // bytes forwarded since connect (status)
  error: string; // last error, or "" (status)
}

let version = 0;
const listeners = new Set<() => void>();
function emit(): void {
  version++;
  for (const l of listeners) l();
}

type N8Globals = {
  __rp_getN8Config?: () => Partial<N8Config>;
  __rp_setN8Port?: (name: string) => void;
  __rp_connectN8?: (enabled: boolean) => void;
  __rp_setN8Lookahead?: (ms: number) => void;
};

/** Whether the host exposes the N8 seam (SDL standalone or DAW plugin). Gates the whole submenu. */
export function hasN8(): boolean {
  return typeof (globalThis as N8Globals).__rp_getN8Config === "function";
}

/** The live serial ports + current N8 link state, read fresh each render. */
export function getN8Config(): N8Config | null {
  const fn = (globalThis as N8Globals).__rp_getN8Config;
  if (typeof fn !== "function") return null;
  const c = fn() ?? {};
  const ports = Array.isArray(c.ports)
    ? c.ports.map((p) => ({ port: typeof p?.port === "string" ? p.port : "", isN8: !!p?.isN8 }))
    : [];
  return {
    ports,
    selectedPort: typeof c.selectedPort === "string" ? c.selectedPort : "",
    connected: !!c.connected,
    enabled: !!c.enabled,
    lookaheadMs: typeof c.lookaheadMs === "number" ? c.lookaheadMs : 0,
    bytes: typeof c.bytes === "number" ? c.bytes : 0,
    error: typeof c.error === "string" ? c.error : "",
  };
}

/** Choose the serial port by name. Applies + persists natively, then repaints the labels. */
export function setN8Port(name: string): void {
  (globalThis as N8Globals).__rp_setN8Port?.(name);
  emit();
}

/** Toggle streaming to the N8 (open/close the serial link). Applies + persists natively. */
export function connectN8(enabled: boolean): void {
  (globalThis as N8Globals).__rp_connectN8?.(enabled);
  emit();
}

/** Set the timed-release lookahead latency (ms). Applies + persists natively. */
export function setN8Lookahead(ms: number): void {
  (globalThis as N8Globals).__rp_setN8Lookahead?.(ms);
  emit();
}

/** A monotonic version - a stable snapshot for App's forced re-render on a pick/toggle. */
export function n8Version(): number {
  return version;
}

export function subscribeN8(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
