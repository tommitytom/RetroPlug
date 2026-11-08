// The control-surface seam for Settings > MIDI's scan and the instance menu's Launchpad submenu.
//
// The native half is the Launchpad pack's FACET (packages/launchpad, over rp.host.devices/2's provider), reached through
// the pack's own client (@retroplug/launchpad/client, createLaunchpadClient) over the app's RPC channel - as n8Devices.ts
// reaches the N8 pack's. Native is a TRANSPORT there and nothing more: it lists ports, claims one pair for the audio
// thread, opens loose ports for a scan, and replays a blob when it lets a device go. Every decision lives in this file:
//
//   1. **Which pair, and whether to take it.** The selection persists in config.json (`controlSurface`), and
//      restoreLaunchpad() reclaims it at startup. Port names are machine state, not project state.
//   2. **The device hint.** The port lists arrive unfiltered, because a Launchpad also speaks TRS/DIN: on a machine
//      short of USB ports it arrives through an ordinary MIDI interface, on a port named after the INTERFACE.
//      PRO_MK3_PORT_HINT only picks a sensible default and tags a row.
//   3. **The close bytes.** Programmer mode locks the device's own Settings menu, so the message that releases it has to
//      survive paths where the audio thread is already stopped. We hand native the bytes with every claim; it replays
//      them on release and on destruct and never parses them.
//   4. **The scan, and what its answers MEAN.** PortScan (@retroplug/launchpad/portScan) writes a Universal Device
//      Inquiry out of each output over the loose-port calls and the replies are parsed HERE, then confirmed against the
//      model's own query on the pair that answered.
//
// So a second controller model - or a different kind of device altogether - is a TS change, never a native one.
import {
  PRO_MK3, PRO_MK3_PORT_HINT, deviceInquiry, deviceName, exitToLiveMode, familyHex, isModelReply, modelQuery,
  parseInquiryReply, profilesForFamily,
} from "@retroplug/launchpad";
import { IDLE_SCAN, PortScan, type MidiPortApi, type ScanStatus } from "@retroplug/launchpad/portScan";
import { createLaunchpadClient, type LaunchpadClient } from "@retroplug/launchpad/client";
import { makeCall } from "../../../src/rpcClient";
import type { UserConfigStore } from "../../../src/userConfigStore";
import { createSubscribable } from "./subscribable";

export interface LaunchpadConfig {
  inputs: string[]; // every hardware MIDI input port, unfiltered
  outputs: string[]; // every hardware MIDI output port, unfiltered
  selectedInput: string; // "" = none chosen
  selectedOutput: string;
  deviceLabel: string; // what a scan identified, e.g. "Launchpad Pro [MK3]" ("" = never scanned)
  connected: boolean; // the in/out pair is claimed
  enabled: boolean; // the user's "use a control surface" toggle
  sent: number; // LED messages written since the claim (status)
  // The link's drops since the claim, one count: a full send ring, an oversized message, and the paints a session keeps
  // pushing while nothing is claimed. The last is what a returning device repaints from, so the number means "what the
  // surface is missing" and not only "a frame the device should have had".
  dropped: number;
  error: string; // why the last claim failed, or "" (status)
}

/** A device a scan found and CONFIRMED: the pair it answered on, and its model's name. */
export interface LaunchpadFind {
  input: string;
  output: string;
  name: string;
}

/** A scan as the menu reads it: the sweep's status, plus what its replies (and the model answers to the follow-up
 *  questions) concluded once it finished. */
export interface LaunchpadScanStatus extends ScanStatus {
  found: LaunchpadFind | null; // a model we drive, confirmed in its own header
  unsupported: string | null; // something Novation answered that we cannot drive, by name
  cancelled: boolean;
}

/** How long to wait for an answer after probing each output. A Device Inquiry reply comes back in milliseconds; this is
 *  generous enough for a DIN chain and short enough that a rig full of ports still scans in about a second. */
export const SCAN_WINDOW_MS = 300;

const notify = createSubscribable();
const emit = notify.emit;
export const subscribeLaunchpad = notify.subscribe;

// The facet's client, probed once: a product mounts its facets at boot and never changes them, so the first answer
// holds for the life of this bundle (one per control-plane context, so each instance probes its own). undefined = not
// probed yet, null = no facet (no RPC channel, or a host that did not mount it: the DAW plugin, the UI test harness,
// an older build).
let facet: LaunchpadClient | null | undefined;

function resolveClient(): LaunchpadClient | null {
  if (facet !== undefined) return facet;
  try {
    const client = createLaunchpadClient(makeCall());
    client.status(); // a host without the facet throws here (no such method)
    facet = client;
  } catch {
    facet = null;
  }
  return facet;
}

/** Replace the facet's client: a fake for a test with no native host (the TS mock tier), null for none, undefined to
 *  probe the RPC channel again. */
export function setLaunchpadClient(client: LaunchpadClient | null | undefined): void {
  facet = client;
}

/** Whether the host mounted the Launchpad facet (the SDL standalone, the CLI and the headless harness do; the DAW plugin
 *  deliberately does not). Everything the facet offers hangs off this: the port pickers, the claim, and the SCAN - which
 *  writes MIDI, so it is never implicit: only Settings > MIDI starts one, and only when the user asks. */
export function hasLaunchpad(): boolean {
  return resolveClient() !== null;
}

/** A port name without the suffix the MIDI backend adds to keep names unique: ALSA's " <client>:<port>" and WinMM's
 *  trailing index (RtMidi appends one to every Windows port). Both move when other devices come and go, so a saved
 *  name is matched on this when the exact name has gone. */
export function portStem(port: string): string {
  return port.replace(/\s+\d+:\d+$/, "").replace(/\s+\d+$/, "");
}

/** True for a port name that looks like the Pro MK3's own USB MIDI interface. The test is on the PORT half of the stem,
 *  not on the whole name and not on a substring: ALSA writes "<card>:<port>" with the card named after the device
 *  ("Launchpad Pro MK3:Launchpad Pro MK3 LPProMK3 MIDI 24:0", or somebody else's box only as the suffix, "Focusrite:
 *  ... LPProMK3 MIDI"), Windows names the device's other two interfaces "MIDIIN2 (LPProMK3 MIDI)" and "MIDIIN3 (...)",
 *  and those must not read as the one to take. False says nothing - a TRS-attached device is a perfectly good Launchpad on
 *  a port named after somebody's interface box. */
export function looksLikeLaunchpad(port: string): boolean {
  const stem = portStem(port);
  const afterClient = stem.slice(stem.indexOf(":") + 1);
  return afterClient.startsWith(PRO_MK3_PORT_HINT) || afterClient.endsWith(` ${PRO_MK3_PORT_HINT}`);
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
 *    - a recorded pick's BOTH ports are currently enumerated. That pick is what a scan writes on a find, and it is the
 *      only thing that can speak for a TRS/DIN device, whose ports are named after somebody's interface. The port list
 *      deliberately keeps a claimed port in it, so a connected device never falls out of this test.
 *
 *  Presence is checked rather than trusted, so an unplugged interface takes the submenu with it - the same rule the N8
 *  Pro submenu next door follows. Settings > MIDI is where a device is found in the first place, and it stays reachable
 *  either way. */
export function launchpadDetected(cfg: LaunchpadConfig | null): boolean {
  if (!cfg) return false; // no seam at all (a DAW-hosted editor / a host that mounts no facet)
  if (cfg.connected) return true;
  if (cfg.inputs.some(looksLikeLaunchpad)) return true;
  return cfg.selectedInput !== "" && cfg.inputs.includes(cfg.selectedInput)
    && cfg.selectedOutput !== "" && cfg.outputs.includes(cfg.selectedOutput);
}

const names = (v: unknown): string[] =>
  Array.isArray(v) ? v.filter((n): n is string => typeof n === "string") : [];

/** The live MIDI ports + current link state, read fresh each render, beside the persisted selection. */
export function getLaunchpadConfig(store: UserConfigStore): LaunchpadConfig | null {
  const client = resolveClient();
  if (!client) return null;
  const ports = client.ports();
  const status = client.status();
  const sel = store.controlSurface();
  return {
    inputs: names(ports.inputs),
    outputs: names(ports.outputs),
    selectedInput: sel.input,
    selectedOutput: sel.output,
    deviceLabel: sel.device,
    connected: status.connected,
    enabled: sel.enabled,
    sent: status.sent,
    dropped: status.dropped,
    error: status.error,
  };
}

/** The bytes that release the device, handed over with every claim (and sent when the project stops driving it). */
function closeBytes(): number[] {
  return Array.from(exitToLiveMode(PRO_MK3));
}

/** Make the link match the stored selection: claim the pair when enabled and both ports are chosen, else let go. A
 *  claim always carries the bytes that release the device, so native can never be holding a device it has no way to
 *  give back. */
function applyLink(store: UserConfigStore): void {
  const client = resolveClient();
  if (!client) return;
  rematchPorts(store);
  const sel = store.controlSurface();
  const status = client.status();
  const want = sel.enabled && sel.input !== "" && sel.output !== "";
  if (want) {
    if (status.connected && status.input === sel.input && status.output === sel.output) return; // already held
    const after = client.claim(sel.input, sel.output, Uint8Array.from(closeBytes()));
    if (!after.connected && after.error)
      console.warn(`[launchpad] could not claim '${sel.input}' / '${sel.output}': ${after.error}`);
  } else if (status.connected || status.error !== "") {
    // A REFUSED claim keeps the pair out of the shared stream on purpose (another RetroPlug holds it), so the link is
    // not connected but the reservation is this process's: Forget, or stopping the connection, must hand it back, or
    // the pair stays out of the stream for the rest of the session with a stale error on the status row.
    client.release();
  }
}

/** A saved port that is not here by its exact name, but is by its stem (portStem) - the same device, renamed because
 *  something else was plugged in - is re-pointed at its new name before the claim. Only on a UNIQUE stem match: two
 *  ports with one stem (two identical interfaces) are left for the user to pick. */
function rematchPorts(store: UserConfigStore): void {
  const sel = store.controlSurface();
  if (sel.input === "" && sel.output === "") return;
  const ports = resolveClient()?.ports();
  if (!ports) return;
  const rematch = (saved: string, list: string[]): string => {
    if (saved === "" || list.includes(saved)) return saved;
    const same = list.filter((p) => portStem(p) === portStem(saved));
    return same.length === 1 ? same[0] : saved;
  };
  const input = rematch(sel.input, names(ports.inputs));
  const output = rematch(sel.output, names(ports.outputs));
  if (input !== sel.input || output !== sel.output) store.setControlSurface({ input, output });
}

/** Reclaim the persisted pair, if it was left connected. Call once when the UI comes up. A port that is not here right
 *  now just fails the claim and shows as an error on the status row.
 *
 *  Then tell the host it is done, whether or not there was anything to claim: the standalone holds its own MIDI stream
 *  and audio back until it hears this (the facet's restored() reaches the product, rp_device_host::restored), so a
 *  restored device's input is reserved before that stream ever opens it and the claim costs no audio restart. */
export function restoreLaunchpad(store: UserConfigStore): void {
  const client = resolveClient();
  if (!client) return;
  try {
    applyLink(store);
  } catch (e) {
    // A claim that throws (an RPC failure, which the native facet catches and answers rather than throwing) must not
    // escape into the caller's mount effect: the host is still waiting for the rendezvous word below, and the menu
    // still has to read as "not connected" rather than failing to build.
    console.warn(`[launchpad] restore failed: ${e}`);
  } finally {
    // Whether or not the claim went through, and even if a step threw (an RPC error in the claim): the product holds its
    // MIDI and audio for this word, and a host that never hears it waits out its bound and starts late for nothing.
    client.restored();
    emit();
  }
}

/** Choose the in/out pair by port name. Persists, and switches live if connected (the old device is told goodbye first,
 *  by the claim that replaces it). */
export function setLaunchpadPorts(store: UserConfigStore, input: string, output: string): void {
  if (launchpadScanBusy()) return; // the scan's answer is about to land here - see launchpadScanBusy
  store.setControlSurface({ input, output });
  applyLink(store);
  emit();
}

/** Take or release the device. Enabling with nothing chosen resolves the hinted default first, so a plugged-in Pro MK3
 *  needs no port picking at all. Enabling with no default leaves it down rather than guessing - the status row says to
 *  pick both ports. */
export function connectLaunchpad(store: UserConfigStore, enabled: boolean, cfg?: LaunchpadConfig | null): void {
  if (launchpadScanBusy()) return;
  if (enabled && cfg && (!cfg.selectedInput || !cfg.selectedOutput)) {
    const input = cfg.selectedInput || defaultPort(cfg.inputs);
    const output = cfg.selectedOutput || defaultPort(cfg.outputs);
    if (input && output) store.setControlSurface({ input, output });
  }
  store.setControlSurface({ enabled });
  applyLink(store);
  emit();
}

/** Forget the recorded device: clears the pair and the label (releasing the device if held), so the instance submenu
 *  goes away again. */
export function forgetLaunchpad(store: UserConfigStore): void {
  if (launchpadScanBusy()) return;
  store.setControlSurface({ input: "", output: "", device: "" });
  applyLink(store);
  emit();
}

// ---- the scan ------------------------------------------------------------------------------------------

/** The loose-port API over the facet: the pool a scan opens its ports in, which is the provider's (the link's claimed
 *  pair is never touched by it). */
function facetPorts(client: LaunchpadClient): MidiPortApi {
  return {
    openInputs: (list) => client.openInputs(list.slice()),
    openOutput: (name) => client.openOutput(name),
    send: (handle, bytes) => client.sendOn(handle, Uint8Array.from(bytes)),
    read: (handle) => client.read(handle).messages.map((m) => Array.from(m)),
    close: (handle) => void client.close(handle),
  };
}

const IDLE: LaunchpadScanStatus = { ...IDLE_SCAN, found: null, unsupported: null, cancelled: false };

let scan: PortScan | null = null;
let lastScan: LaunchpadScanStatus = IDLE;
// The finished scan whose answer has been recorded. The poll's own latch resets when the UI remounts, and a finished
// scan re-applied then would stomp whatever the user picked since.
let appliedScan: LaunchpadScanStatus | null = null;
let clock: () => number = () => Date.now();

/** Replace the scan's clock. Tests only - the scan must be steppable a millisecond at a time. */
export function setLaunchpadScanClock(fn: () => number): void {
  clock = fn;
}

/** Whether a scan is running. While it is, the selection is the scan's to write: Connect, the port pickers and Forget
 *  are greyed in the menu and refuse here too. Otherwise a pick made mid-scan would be overwritten the moment the scan
 *  settles, and on Windows a Connect would fail outright - the scan holds the inputs it listens on, and a Windows MIDI
 *  input is exclusive. A scan takes about a second; Cancel ends it sooner. */
export function launchpadScanBusy(): boolean {
  return scan !== null && scan.status().busy;
}

/** The model questions a reply earns: one per profile whose family bytes it carries. Asked on the same pair while the
 *  ports are open (PortScan's follow-up), because Novation reuses family bytes across models - the Mini MK3 and the Pro
 *  MK3 both answer 13 01 - and a device ignores a message in another model's header. */
function askModels(reply: { bytes: number[] }): number[][] {
  const parsed = parseInquiryReply(reply.bytes);
  return parsed ? profilesForFamily(parsed.family).map((p) => modelQuery(p)) : [];
}

/** Ask every output "what are you?" and listen on every input. Returns whether a scan started - refused while one is
 *  running, or while the link holds a pair (those ports cannot be reopened, and a connected device needs no finding). */
export function startLaunchpadScan(): boolean {
  const client = resolveClient();
  if (!client || launchpadScanBusy() || client.status().connected) return false;
  const ports = client.ports();
  scan = new PortScan(facetPorts(client), names(ports.inputs), names(ports.outputs), deviceInquiry(), SCAN_WINDOW_MS,
    { followUp: askModels });
  scan.step(clock()); // the first probe goes out now, not a poll later
  publish(scan.status(), false);
  emit();
  return true;
}

// The same action the Settings > MIDI row takes, as a global: automation (and tools/run-sdl-controller.sh, through the
// standalone's RETROPLUG_SDL_TEST_SCAN hook) can start a scan without driving the menu.
(globalThis as { __rp_scanControlSurface?: () => boolean }).__rp_scanControlSurface = () => startLaunchpadScan();

/** End a running scan now. Nothing it found is recorded - a scan the user walked away from does not get to rewrite their
 *  selection afterwards. */
export function cancelLaunchpadScan(): void {
  if (!scan || !scan.status().busy) return;
  scan.cancel();
  publish(scan.status(), true);
  scan = null;
  appliedScan = lastScan;
  emit();
}

/** Advance a running scan and return where it is. The frame poll (useLaunchpadScanWatch) calls this; it is what moves the
 *  scan along, so nothing runs while nothing polls. null without the facet. */
export function pollLaunchpadScan(): LaunchpadScanStatus | null {
  if (!hasLaunchpad()) return null;
  if (scan) {
    scan.step(clock());
    publish(scan.status(), false);
    if (!lastScan.busy) scan = null;
  }
  return lastScan;
}

/** What a finished scan concluded: the first reply whose model ANSWERED its own query on the same pair, and failing that,
 *  the first Novation thing we cannot drive. */
function conclude(st: ScanStatus): { found: LaunchpadFind | null; unsupported: string | null } {
  let unsupported: string | null = null;
  for (const r of st.replies) {
    const parsed = parseInquiryReply(r.bytes);
    if (!parsed) continue;
    const candidates = profilesForFamily(parsed.family);
    for (const profile of candidates) {
      const answered = st.replies.some((x) => x.output === r.output && x.input === r.input && isModelReply(profile, x.bytes));
      if (answered) return { found: { input: r.input, output: r.output, name: profile.name }, unsupported: null };
    }
    if (unsupported !== null) continue;
    unsupported = candidates.length === 0
      ? deviceName(parsed.family)
      // Answered the inquiry with a profiled family's bytes, but none of those models answered in its own header:
      // another model of the same family (a Mini MK3, for the Pro MK3's 13 01).
      : `Novation device (family ${familyHex(parsed.family)}, not a ${candidates.map((c) => c.name).join(" / ")})`;
  }
  return { found: null, unsupported };
}

/** Rebuild the menu's view of the scan, as a NEW object only when the scan's status object changed (the apply latch and
 *  the poll's re-render both key off identity / version). */
let publishedFrom: ScanStatus | null = null;
function publish(st: ScanStatus, cancelled: boolean): void {
  if (st === publishedFrom && cancelled === lastScan.cancelled) return;
  publishedFrom = st;
  const verdict = st.busy || cancelled ? { found: null, unsupported: null } : conclude(st);
  lastScan = { ...st, ...verdict, cancelled, version: lastScan.version + 1 };
}

/** The current/last scan without advancing it - what a menu render reads. null without the facet. */
export function getLaunchpadScan(): LaunchpadScanStatus | null {
  return hasLaunchpad() ? lastScan : null;
}

/** Record what a finished scan found: the pair it answered on becomes the selection, so the device is configured and the
 *  instance submenu appears. Returns the find, or null when nothing was confirmed. */
export function applyLaunchpadScan(store: UserConfigStore, s: LaunchpadScanStatus): LaunchpadFind | null {
  if (s === appliedScan || s.busy) return null;
  appliedScan = s;
  const hit = s.found;
  if (!hit) return null;
  store.setControlSurface({ input: hit.input, output: hit.output, device: hit.name });
  applyLink(store);
  emit();
  return hit;
}

// ---- letting the device go when nothing drives it ----------------------------------------------------------

/** Polls to wait, once the project stops wanting the controller, before releasing the device - long enough for the role's
 *  last blocks to have been written out, so its final LED frame cannot land after the release. */
export const RELEASE_DELAY_POLLS = 2;
let roleWanted = false;
let releaseIn = 0;

/** Called from the frame poll with whether the open project runs the controller (`settings.controller.enabled`).
 *
 *  The role that drives the device lives in the plan, and when the project stops wanting it - the feature switched off,
 *  or a project without it loaded - the plan simply drops it: nothing there can say goodbye. Left alone, a Launchpad
 *  stays in Programmer mode with its last LEDs frozen and its own front panel locked, while the status row goes on
 *  reading "connected". So the claim's close bytes are sent here, with the pair still claimed: turning the feature back
 *  on brings a fresh role, which takes the device again on its own (it re-enters Programmer mode and repaints on the
 *  link edge it sees). */
export function pollControllerRole(wanted: boolean): void {
  const client = resolveClient();
  if (!client) return;
  if (roleWanted && !wanted) releaseIn = RELEASE_DELAY_POLLS;
  if (wanted) releaseIn = 0;
  roleWanted = wanted;
  if (releaseIn > 0 && --releaseIn === 0 && client.status().connected)
    client.send(Uint8Array.from(closeBytes()));
}

/** Drop the module's scan state. Tests only: each case starts from no scan at all. */
export function resetLaunchpadScanForTest(): void {
  scan?.cancel();
  scan = null;
  lastScan = IDLE;
  appliedScan = null;
  publishedFrom = null;
  roleWanted = false;
  releaseIn = 0;
  clock = () => Date.now();
}
