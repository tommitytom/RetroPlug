// "Control surface" config for the Launchpad submenu (in the instance menu's tracker block, beside N8 Pro).
// Standalone-only: the DAW plugin's MIDI seam still caps a message at 4 bytes and every message here is
// SysEx, so bindLaunchpadHooks is called from sdl/main.cpp alone.
//
// Mirrors midiDevices.ts / n8Devices.ts: the state lives in the native host (the claimed in/out pair, the
// enabled toggle, the persisted launchpad.cfg), not a TS store, so this is a subscribable rather than a
// store. Picks and toggles apply immediately (the host reconnects + persists on the spot) and App subscribes
// (subscribeLaunchpad) so the labels track the new value at once.
//
// TWO THINGS THIS FILE OWNS THAT NATIVE DELIBERATELY DOES NOT:
//
//   1. **The device hint.** The port lists arrive unfiltered, because a Launchpad also speaks TRS/DIN: on a
//      machine short of USB ports it arrives through an ordinary MIDI interface, on a port named after the
//      INTERFACE. Nothing in that name says "Launchpad", so filtering natively would make exactly that setup
//      unconfigurable. PRO_MK3_PORT_HINT only picks a sensible default and tags a row.
//   2. **The farewell.** Programmer mode locks the device's own Settings menu, so the message that releases
//      it has to survive a path where the audio thread is already stopped. We hand native the bytes; it
//      replays them on disconnect and on destruct and never parses them. Native learning the protocol is
//      precisely what that avoids.
//
// See __rp_getLaunchpadConfig / __rp_setLaunchpadPorts / __rp_connectLaunchpad / __rp_setLaunchpadFarewell,
// bound by bindLaunchpadHooks (packages/native/src/host/launchpad/LaunchpadHooks.cpp).

import { PRO_MK3, PRO_MK3_PORT_HINT, exitToLiveMode } from "../../../src/launchpad";

export interface LaunchpadConfig {
  inputs: string[]; // every hardware MIDI input port, unfiltered
  outputs: string[]; // every hardware MIDI output port, unfiltered
  selectedInput: string; // "" = none chosen
  selectedOutput: string;
  connected: boolean; // the in/out pair is claimed
  enabled: boolean; // the user's "use a control surface" toggle
  sent: number; // LED messages written since connect (status)
  dropped: number; // messages lost to a full ring / an oversized message (status)
  error: string; // last error, or "" (status)
}

let version = 0;
const listeners = new Set<() => void>();
function emit(): void {
  version++;
  for (const l of listeners) l();
}

type LaunchpadGlobals = {
  __rp_getLaunchpadConfig?: () => Partial<LaunchpadConfig>;
  __rp_setLaunchpadPorts?: (input: string, output: string) => void;
  __rp_connectLaunchpad?: (enabled: boolean) => void;
  __rp_setLaunchpadFarewell?: (bytes: number[]) => void;
};

/** Whether the host exposes the control-surface seam (the SDL standalone). */
export function hasLaunchpad(): boolean {
  return typeof (globalThis as LaunchpadGlobals).__rp_getLaunchpadConfig === "function";
}

/** True for a port name that looks like the Pro MK3's own USB MIDI interface. False says nothing: a
 *  TRS-attached device is a perfectly good Launchpad on a port named after somebody's interface box. */
export function looksLikeLaunchpad(port: string): boolean {
  return port.includes(PRO_MK3_PORT_HINT);
}

/** The port to offer first: the hinted one if it is here, else nothing (the user picks). */
export function defaultPort(ports: readonly string[]): string {
  return ports.find(looksLikeLaunchpad) ?? "";
}

/** The hook we have already told. Keyed on the FUNCTION rather than a boolean, so a rebound seam (a fresh
 *  host context) is told again - a latch would leave the new one holding nothing. */
let farewellTold: unknown = null;

/** Hand native the bytes that release the device. Called before anything can connect, so the link is never
 *  holding a device it has no way to give back. */
function ensureFarewell(): void {
  const fn = (globalThis as LaunchpadGlobals).__rp_setLaunchpadFarewell;
  if (typeof fn !== "function" || fn === farewellTold) return;
  fn(exitToLiveMode(PRO_MK3));
  farewellTold = fn;
}

/** The live MIDI ports + current link state, read fresh each render. */
export function getLaunchpadConfig(): LaunchpadConfig | null {
  const fn = (globalThis as LaunchpadGlobals).__rp_getLaunchpadConfig;
  if (typeof fn !== "function") return null;
  ensureFarewell(); // the menu reads this every render, so the blob lands long before any Connect
  const c = fn() ?? {};
  const names = (v: unknown): string[] =>
    Array.isArray(v) ? v.filter((n): n is string => typeof n === "string") : [];
  return {
    inputs: names(c.inputs),
    outputs: names(c.outputs),
    selectedInput: typeof c.selectedInput === "string" ? c.selectedInput : "",
    selectedOutput: typeof c.selectedOutput === "string" ? c.selectedOutput : "",
    connected: !!c.connected,
    enabled: !!c.enabled,
    sent: typeof c.sent === "number" ? c.sent : 0,
    dropped: typeof c.dropped === "number" ? c.dropped : 0,
    error: typeof c.error === "string" ? c.error : "",
  };
}

/** Choose the in/out pair by port name. Applies + persists natively, then repaints the labels. */
export function setLaunchpadPorts(input: string, output: string): void {
  (globalThis as LaunchpadGlobals).__rp_setLaunchpadPorts?.(input, output);
  emit();
}

/** Take or release the device. Enabling with nothing chosen resolves the hinted default first, so a plugged-in
 *  Pro MK3 needs no port picking at all. */
export function connectLaunchpad(enabled: boolean, cfg?: LaunchpadConfig | null): void {
  ensureFarewell();
  if (enabled && cfg && (!cfg.selectedInput || !cfg.selectedOutput)) {
    const input = cfg.selectedInput || defaultPort(cfg.inputs);
    const output = cfg.selectedOutput || defaultPort(cfg.outputs);
    if (input && output) setLaunchpadPorts(input, output);
  }
  (globalThis as LaunchpadGlobals).__rp_connectLaunchpad?.(enabled);
  emit();
}

/** A monotonic version - a stable snapshot for App's forced re-render on a pick/toggle. */
export function launchpadVersion(): number {
  return version;
}

export function subscribeLaunchpad(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
