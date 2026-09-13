// "Stream to a physical Everdrive N8 Pro" config for the N8 Pro submenu (in the instance menu's tracker
// block, beside risa/LSDj), available in BOTH the SDL
// standalone and the DAW plugin (bindN8Hooks binds the __rp_*N8* hooks in each host over its own N8Host).
// Mirrors midiDevices.ts: the state lives in the native host (the serial connection + lookahead + persisted
// n8.cfg), not a TS store, so this is a subscribable, not a store. Picks/toggles apply immediately (the host
// (re)connects the serial port + persists on the spot); App subscribes (subscribeN8) so the labels track the
// new value at once. The seam is present in both hosts and absent only in the headless harness (hasN8() then
// false -> submenu hidden). See __rp_getN8Config / __rp_setN8Port / __rp_connectN8 / __rp_setN8Lookahead, bound
// by bindN8Hooks (packages/native/src/host/n8/N8Hooks.cpp) from both sdl/main.cpp and plugin/PluginDSP.cpp.
//
// The one piece of N8 config that is NOT host state is the cartridge sound chip's level: that belongs to the
// NES system (its "Expansion Volume" knob, in the project), and App pushes it down here - see
// projectExpVolForN8 / setN8ExpVol at the bottom.
import type { SystemView } from "../../../src/systemsStore";

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
  __rp_setN8ExpVol?: (v: number) => void;
};

/** Whether the host exposes the N8 seam (SDL standalone or DAW plugin). Necessary but not sufficient for the
 *  submenu: menuDefs only renders it when a physical N8 is actually detected (n8Detected over getN8Config). */
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

/** The N8's expansion-audio master volume (`master_vol`, 128 = unity) for a system "Expansion Volume" of
 *  `percent` (100 = unity). Both scale the cartridge's own sound chip, just against different unities, so
 *  the emulated cart and the console end up at the same level. 200% saturates the register. */
export function expVolToN8(percent: number): number {
  return Math.max(0, Math.min(255, Math.round((percent * 128) / 100)));
}

/** Point a connected N8 at the level the project asks for (-1 = leave it to the host's own default, which
 *  derives unity from the running cart's mapper - what a project with no NES system in it gets). Applied on
 *  the spot when the link is up, and again on every connect. NOT persisted here: the level belongs to the
 *  NES system's `expansionVolume` knob, in the project. */
export function setN8ExpVol(v: number): void {
  (globalThis as N8Globals).__rp_setN8ExpVol?.(v);
}

/** The N8 master_vol a project asks for: the focused NES system's "Expansion Volume" (else the first NES
 *  system's), rescaled. -1 when the project has no NES system at all - there is no cart here to take a level
 *  from, so the host goes on deriving one from whatever is running on the console. Several NES instances can
 *  share one link (it carries the whole instance's MIDI), so the focused one - the cart whose menu you were
 *  just in - decides. */
export function projectExpVolForN8(systems: SystemView[]): number {
  const nes = systems.filter((s) => s.platform === "nes");
  if (nes.length === 0) return -1;
  const sys = nes.find((s) => s.focused) ?? nes[0];
  const cfg = sys.roles.find((r) => r.kind === "mesen")?.config as Record<string, unknown> | undefined;
  const percent = typeof cfg?.expansionVolume === "number" ? cfg.expansionVolume : 100;
  return expVolToN8(percent);
}

/** A monotonic version - a stable snapshot for App's forced re-render on a pick/toggle. */
export function n8Version(): number {
  return version;
}

export function subscribeN8(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
