// "Control surface" config for the Launchpad submenu (in the instance menu's tracker block, beside N8 Pro).
// Standalone-only: the DAW plugin's MIDI seam still caps a message at 4 bytes and every message here is
// SysEx, so bindLaunchpadHooks is called from sdl/main.cpp alone.
//
// Mirrors midiDevices.ts / n8Devices.ts: the state lives in the native host (the claimed in/out pair, the
// enabled toggle, the persisted launchpad.cfg), not a TS store, so this is a subscribable rather than a
// store. Picks and toggles apply immediately (the host reconnects + persists on the spot) and App subscribes
// (subscribeLaunchpad) so the labels track the new value at once.
//
// THREE THINGS THIS FILE OWNS THAT NATIVE DELIBERATELY DOES NOT:
//
//   1. **The device hint.** The port lists arrive unfiltered, because a Launchpad also speaks TRS/DIN: on a
//      machine short of USB ports it arrives through an ordinary MIDI interface, on a port named after the
//      INTERFACE. Nothing in that name says "Launchpad", so filtering natively would make exactly that setup
//      unconfigurable. PRO_MK3_PORT_HINT only picks a sensible default and tags a row.
//   2. **The farewell.** Programmer mode locks the device's own Settings menu, so the message that releases
//      it has to survive a path where the audio thread is already stopped. We hand native the bytes; it
//      replays them on disconnect and on destruct and never parses them. Native learning the protocol is
//      precisely what that avoids.
//   3. **The scan, and what its answers MEAN.** Native writes an opaque probe out of each output and reports
//      what came back on each input; the probe is a Universal Device Inquiry and the replies are parsed HERE
//      (parseInquiryReply / deviceName). That is what finds a device on a port whose name says nothing, and
//      it is the only way to learn the PAIRING - the reply names the input that belongs to the output we
//      wrote to, which may be a different interface entirely.
//
// See __rp_getLaunchpadConfig / __rp_setLaunchpadPorts / __rp_connectLaunchpad / __rp_setLaunchpadFarewell /
// __rp_scanLaunchpad / __rp_getLaunchpadScan / __rp_setLaunchpadDevice, bound by bindLaunchpadHooks
// (packages/native/src/host/launchpad/LaunchpadHooks.cpp).

import {
  PRO_MK3, PRO_MK3_PORT_HINT, deviceInquiry, deviceName, exitToLiveMode, parseInquiryReply,
} from "../../../src/launchpad";
import { createSubscribable } from "./subscribable";

export interface LaunchpadConfig {
  inputs: string[]; // every hardware MIDI input port, unfiltered
  outputs: string[]; // every hardware MIDI output port, unfiltered
  selectedInput: string; // "" = none chosen
  selectedOutput: string;
  deviceLabel: string; // what a scan identified, e.g. "Launchpad Pro [MK3]" ("" = never scanned)
  connected: boolean; // the in/out pair is claimed
  enabled: boolean; // the user's "use a control surface" toggle
  sent: number; // LED messages written since connect (status)
  dropped: number; // messages lost to a full ring / an oversized message (status)
  error: string; // last error, or "" (status)
}

/** One message an input returned while a given output was being probed. `bytes` is whatever arrived. */
export interface LaunchpadScanReply {
  output: string;
  input: string;
  bytes: number[];
}

export interface LaunchpadScanStatus {
  busy: boolean;
  done: boolean; // a scan finished (with or without a find)
  phase: string; // short label while running, e.g. "Probing MIDISPORT 2x2 Port A"
  error: string;
  skipped: number; // ports that would not open (another app holds them)
  version: number; // bumped on any change - the UI polls this, not the contents
  replies: LaunchpadScanReply[];
}

/** How long to wait for an answer after probing each output. A Device Inquiry reply comes back in
 *  milliseconds; this is generous enough for a DIN chain and short enough that a rig full of ports still
 *  scans in about a second. */
export const SCAN_WINDOW_MS = 300;

const notify = createSubscribable();
const emit = notify.emit;
export const subscribeLaunchpad = notify.subscribe;

type LaunchpadGlobals = {
  __rp_getLaunchpadConfig?: () => Partial<LaunchpadConfig>;
  __rp_setLaunchpadPorts?: (input: string, output: string) => void;
  __rp_connectLaunchpad?: (enabled: boolean) => void;
  __rp_setLaunchpadFarewell?: (bytes: number[]) => void;
  __rp_scanLaunchpad?: (probe: number[], windowMs: number) => boolean;
  __rp_getLaunchpadScan?: () => Partial<LaunchpadScanStatus>;
  __rp_setLaunchpadDevice?: (label: string) => void;
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

/** Whether a control surface is actually HERE - what gates the instance menu's Launchpad submenu.
 *
 *  Three ways to be sure, none of which probe anything (the scan is user-initiated, never a render):
 *
 *    - a port named like the device's own USB interface is enumerated, so it is plugged in over USB;
 *    - the link is holding a pair right now;
 *    - a recorded pick's BOTH ports are currently enumerated. That pick is what a scan writes on a find,
 *      and it is the only thing that can speak for a TRS/DIN device, whose ports are named after somebody's
 *      interface. `listInputs` deliberately keeps a claimed port in the list, so a connected device never
 *      falls out of this test.
 *
 *  Presence is checked rather than trusted, so an unplugged interface takes the submenu with it - the same
 *  rule the N8 Pro submenu next door follows. Settings > MIDI is where a device is found in the first place,
 *  and it stays reachable either way. */
export function launchpadDetected(cfg: LaunchpadConfig | null): boolean {
  if (!cfg) return false; // no seam at all (a DAW-hosted editor / the headless harness)
  if (cfg.connected) return true;
  if (cfg.inputs.some(looksLikeLaunchpad)) return true;
  return cfg.selectedInput !== "" && cfg.inputs.includes(cfg.selectedInput)
    && cfg.selectedOutput !== "" && cfg.outputs.includes(cfg.selectedOutput);
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
    deviceLabel: typeof c.deviceLabel === "string" ? c.deviceLabel : "",
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

/** Forget the recorded device: clears the pair and the label, so the instance submenu goes away again. */
export function forgetLaunchpad(): void {
  (globalThis as LaunchpadGlobals).__rp_setLaunchpadPorts?.("", "");
  (globalThis as LaunchpadGlobals).__rp_setLaunchpadDevice?.("");
  emit();
}

/** Whether the host can scan (the SDL standalone). Scanning writes MIDI, so it is never implicit: only
 *  Settings > MIDI starts one, and only when the user asks. */
export function hasLaunchpadScan(): boolean {
  return typeof (globalThis as LaunchpadGlobals).__rp_scanLaunchpad === "function";
}

/** Ask every output "what are you?" and listen on every input. Returns whether a scan started - native
 *  refuses while one is running, or while the link holds a pair (those ports cannot be reopened, and a
 *  connected device needs no finding). */
export function startLaunchpadScan(): boolean {
  const started = (globalThis as LaunchpadGlobals).__rp_scanLaunchpad?.(deviceInquiry(), SCAN_WINDOW_MS) ?? false;
  if (started) emit();
  return started;
}

/** The live scan state, read fresh. null without the seam. */
export function getLaunchpadScan(): LaunchpadScanStatus | null {
  const fn = (globalThis as LaunchpadGlobals).__rp_getLaunchpadScan;
  if (typeof fn !== "function") return null;
  const s = fn() ?? {};
  const replies = Array.isArray(s.replies) ? s.replies : [];
  return {
    busy: !!s.busy,
    done: !!s.done,
    phase: typeof s.phase === "string" ? s.phase : "",
    error: typeof s.error === "string" ? s.error : "",
    skipped: typeof s.skipped === "number" ? s.skipped : 0,
    version: typeof s.version === "number" ? s.version : 0,
    replies: replies.map((r) => ({
      output: typeof r?.output === "string" ? r.output : "",
      input: typeof r?.input === "string" ? r.input : "",
      bytes: Array.isArray(r?.bytes) ? r.bytes.filter((b): b is number => typeof b === "number") : [],
    })),
  };
}

/** The first reply that is a Device Inquiry answer from a Novation, as the pair it arrived on plus a name.
 *  Everything else a scan collected - a keyboard's stray notes, another manufacturer answering the same
 *  universal question - falls out here, in TS, which is the only layer that knows the difference. */
export function launchpadFromScan(s: LaunchpadScanStatus): { input: string; output: string; name: string } | null {
  for (const r of s.replies) {
    const parsed = parseInquiryReply(r.bytes);
    if (parsed) return { input: r.input, output: r.output, name: deviceName(parsed.family) };
  }
  return null;
}

/** Record what a finished scan found: the pair it answered on becomes the selection, so the device is
 *  configured and the instance submenu appears. Returns the find, or null when nothing answered. */
export function applyLaunchpadScan(s: LaunchpadScanStatus): { input: string; output: string; name: string } | null {
  const hit = launchpadFromScan(s);
  if (!hit) return null;
  const g = globalThis as LaunchpadGlobals;
  g.__rp_setLaunchpadPorts?.(hit.input, hit.output);
  g.__rp_setLaunchpadDevice?.(hit.name);
  emit();
  return hit;
}


