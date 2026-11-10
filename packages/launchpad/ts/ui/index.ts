// The Launchpad's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.4 and 4.5): the control surface's
// submenu in the instance menu's device block, the scan rows Settings > MIDI offers (the one place a Launchpad can be
// FOUND), and the service that steps a scan and reclaims the recorded pair at startup.
//
// The submenu appears only when a surface is actually there - over USB by port name, or on a port pair a Settings >
// MIDI scan recorded (launchpadDetected). Detecting one is not a name test, because a Launchpad also speaks TRS/DIN: on
// a machine short of USB ports it arrives through an ordinary MIDI interface, on a port named after the INTERFACE, and
// nothing about that name says "Launchpad". Nor can it be done passively - DIN has no enumeration and no idle
// heartbeat, so the OS sees the interface and nothing else. It takes a Universal Device Inquiry, which is a transmit,
// which is why FINDING one lives in Settings > MIDI as a deliberate user action and this submenu only ever reads the
// answer. The port cyclers still offer every port unfiltered, so a wrong guess is fixable here.
//
// The rows straddle two scopes on purpose, because setting the feature up needs both: the ports + Connect are HOST
// state (persisted in config.json's controlSurface, shared by every project), while the app, its own knobs and Enable
// are PROJECT state (persisted in the .rplg, since which cart a surface drives is a property of the project). The shell
// hands both scopes in as the host's `config` and `project` groups; the `controller` section this pack contributes is
// what `project` reads and patches.
//
// The app's rows are built from the registry: the project's app id picks the contribution, and the knobs are the ones
// ITS module declares (menu), defaulted and bounded by ITS schema. So a second app is a package's change, not a
// shell one.
import { action, cycler, infoRow, separator, submenu, type MenuItem } from "@retroplug/ui/menu";
import {
  configRowCycler, defineUi, type MenuBuilder, type UiHost, type UiService,
} from "@retroplug/ui/contribution";
import { CONTROLLER_TARGET_VALUES, type ControllerTarget } from "../enums";
import { CONTROLLER_SECTION_KEY, type ControllerSection } from "../control/projectSection";
import { controllerApps, type ControllerAppContribution } from "../registry";
import { controllerSyncOverride } from "../control/placer";
import type { ControllerServiceState } from "../control/projectService";
import {
  applyLaunchpadScan, cancelLaunchpadScan, connectLaunchpad, forgetLaunchpad, getLaunchpadConfig,
  getLaunchpadScan, hasLaunchpad, launchpadDetected, launchpadScanBusy, looksLikeLaunchpad, pollControllerRole,
  onResetLaunchpadScanForTest, pollLaunchpadScan, restoreLaunchpad, setLaunchpadMountRpc, setLaunchpadPorts,
  startLaunchpadScan, subscribeLaunchpad, type LaunchpadConfig, type LaunchpadScanStatus,
} from "./launchpadDevices";

const CONTROLLER_TARGET_LABELS: Record<ControllerTarget, string> = {
  system: "Emulated Cart",
  midiOut: "MIDI Out (real hardware)",
};

/** The read-only line describing what the controller has done to the cart's sync role. `driving` is the app's own answer
 *  (controllerSyncOverride): the mode its launches need is the one the cart is being put in, which is NOT "the mode is
 *  not the string off" - an app whose off-mode is spelt differently, or that has none, would then read as driving while
 *  nothing was sent. `cartSync` is the cart's OWN SYNC setting as last polled, which is what decided it. The wording
 *  names the APP and no mode, so a second app needs no change here. */
function cartSyncLabel(app: string, driving: boolean, cartSync: string | null): string {
  if (driving) return cartSync ? `Cart: ${app} - driving it` : `Cart: ${app} - driving it (SYNC unread)`;
  return `Cart: ${app} - not driving it: its SYNC is ${cartSync ?? "unread"}`;
}

/** The read-only status line: what the link is doing, in the words a player would use. */
function launchpadStatus(cfg: LaunchpadConfig): string {
  if (cfg.connected) {
    // `dropped` is what the device never got while it WAS attached (a full send ring, an oversized message): frames the
    // surface is missing, which a repaint replaces but the row still reports.
    if (cfg.dropped > 0) return `connected (${cfg.dropped} dropped)`;
    // The name a scan read off the device, when there is one. Worth the room: it is the difference between
    // "the cable reaches a Launchpad" and "the ports opened", which otherwise read identically here.
    return cfg.deviceLabel ? `connected - ${cfg.deviceLabel}` : "connected";
  }
  if (cfg.error) return `Error: ${cfg.error}`;
  if (cfg.enabled) return !cfg.selectedInput || !cfg.selectedOutput ? "pick both ports" : "not connected";
  // Nothing chosen, the feature off, and a device on a port named like its own USB interface: the whole of what an
  // upgrade from the legacy host needs is that there is nothing to migrate and where a device is found (design
  // section 5). The scan lives in Settings > MIDI because this submenu only exists for a device already known.
  if (!cfg.selectedInput && !cfg.selectedOutput && cfg.inputs.some(looksLikeLaunchpad))
    return "a Launchpad is here - Connect, or Settings > MIDI to scan for one";
  return "not connected";
}

/** Cycler names for one direction's ports, hinted ones tagged. Index 0 is "(none)" - a port must be chosen,
 *  since there is no sensible "all devices" for a device we claim exclusively. */
function launchpadPortNames(ports: string[], selected: string): { names: string[]; index: number } {
  const label = (p: string) => (looksLikeLaunchpad(p) ? `${p} (detected)` : p);
  const names = ["(none)", ...ports.map(label)];
  if (selected === "") return { names, index: 0 };
  const i = ports.indexOf(selected);
  if (i >= 0) return { names, index: i + 1 };
  names.push(`${selected} (not connected)`); // a persisted pick whose device is away still shows
  return { names, index: names.length - 1 };
}

/** The app's own rows, built from the registry and the app's schema: one per knob the app declares (`menu`), so the
 *  shell names no app, no knob and no value. They show what the app RUNS rather than what the project literally holds:
 *  the stored config goes through the app's own parse, the same one parseAppConfig hands the runtime, so a hand-edited
 *  value the schema snaps to its default shows the snapped one, and a step writes the next value of that same list.
 *  `defaults` is the schema's default per knob, the fallback readConfigRow takes for a value of the wrong type. */
function controllerAppRows(
  host: UiHost,
  app: ControllerAppContribution | undefined,
  config: Record<string, unknown>,
): MenuItem[] {
  if (!app || !app.menu) return [];
  const running = app.config.parse(config); // exactly what parseAppConfig(app.id, config) hands the runtime
  const defaults = app.config.parse({}); // the schema is the source of a knob's default, as it is of its bounds
  return app.menu.map((field) =>
    configRowCycler(`lp-app-${field.key}`, field, running[field.key], defaults[field.key], (value) =>
      host.project.setSection(CONTROLLER_SECTION_KEY, { appConfig: { [field.key]: value } })),
  );
}

function launchpadMenuChildren(host: UiHost, cfg: LaunchpadConfig): MenuItem[] {
  const controller = host.project.section<ControllerSection>(CONTROLLER_SECTION_KEY);
  const store = host.config;
  const inp = launchpadPortNames(cfg.inputs, cfg.selectedInput);
  const out = launchpadPortNames(cfg.outputs, cfg.selectedOutput);
  const portAt = (ports: string[], n: number, current: string) => (n === 0 ? "" : ports[n - 1] ?? current);
  const rpc = host.devices.rpc;

  // Polled on the song-watch timer (the controller's project service), so reading it here costs a field access
  // rather than a 128 KB battery copy per menu render.
  const service = host.project.serviceState<ControllerServiceState>(CONTROLLER_SECTION_KEY);
  const cartSync = controller.enabled ? (service?.cartSync ?? null) : null;
  // What the published document will have the cart's sync node run, computed by the SAME helper the pack's placer uses
  // - so the row cannot describe something other than what is happening.
  const syncOverride = controller.enabled
    ? controllerSyncOverride(host.systems.view(), { ...controller, cartSync })
    : null;
  const apps = controllerApps.list();
  const appIdx = Math.max(0, apps.findIndex((a) => a.id === controller.app));
  const app = apps.find((a) => a.id === controller.app);

  // Greyed while a scan runs: its answer is about to become the selection (launchpadScanBusy). The port pickers are
  // greyed while connected too: stepping one live would claim every pair it passed - Programmer-mode bytes at whatever
  // device each step landed on. Disconnect, pick, Connect.
  const scanning = launchpadScanBusy();
  const pickable = (item: MenuItem): MenuItem => (scanning || cfg.connected ? { ...item, disabled: true } : item);

  return [
    pickable(cycler("lp-input", "Input Device", inp.names, inp.index, (n) =>
      setLaunchpadPorts(store, portAt(cfg.inputs, n, cfg.selectedInput), cfg.selectedOutput, rpc))),
    pickable(cycler("lp-output", "Output Device", out.names, out.index, (n) =>
      setLaunchpadPorts(store, cfg.selectedInput, portAt(cfg.outputs, n, cfg.selectedOutput), rpc))),
    // Keyed on what the link IS doing, not on the saved intent: a pair saved as connected whose device was away at
    // launch is not connected, and reading "Disconnect" there left no way to retry but pressing twice. So it offers
    // Connect (which retries) beside a way to stop it reconnecting at every launch.
    action("lp-connect", cfg.connected ? "Disconnect" : "Connect",
      () => connectLaunchpad(store, !cfg.connected, cfg, rpc), scanning),
    ...(cfg.enabled && !cfg.connected
      ? [action("lp-stop", "Stop Connecting at Launch", () => connectLaunchpad(store, false, cfg, rpc), scanning)]
      : []),
    infoRow("lp-status", `Status: ${launchpadStatus(cfg)}`), // read-only
    // What the controller is doing to the cart's sync role, and why. Shown rather than applied silently: the override
    // does not touch the saved project (it happens in the published document), so without a row here the LSDj submenu
    // would go on reporting the user's own setting while the cart ran a different one.
    //
    // A cart that is not in the mode the app needs ignores row launches ENTIRELY WITHOUT COMPLAINT - it goes on playing
    // and stepping to the host clock, so the only symptom is that the pads stop doing anything. And the way players land
    // there is nasty: LSDj refuses to change SYNC while the cart is playing, so reaching for the setting mid-session
    // leaves it wherever the first press landed, with no way back until playback stops (measured in
    // test-native/lsdj-sync-toggle).
    ...(syncOverride
      ? [infoRow("lp-cart-sync", cartSyncLabel(app?.label ?? controller.app, syncOverride.driving, cartSync))]
      : []),
    separator("lp-sep-app"),
    // Project scope from here down: these ride the .rplg, and each edit publishes the graph.
    cycler("lp-enabled", "Use in Project", ["Off", "On"], controller.enabled ? 1 : 0, (n) =>
      host.project.setSection(CONTROLLER_SECTION_KEY, { enabled: n === 1 })),
    cycler("lp-app", "App", apps.map((a) => a.label), appIdx, (n) =>
      host.project.setSection(CONTROLLER_SECTION_KEY, { app: apps[n].id, appConfig: {} })),
    ...controllerAppRows(host, app, controller.appConfig),
    // Where launches go. "Emulated Cart" hands them to the tracker's own sync role in the same block; "MIDI Out" sends
    // them out of the host, which is how a real Game Boy behind an Arduinoboy is driven.
    cycler("lp-target", "Launches To", CONTROLLER_TARGET_VALUES.map((t) => CONTROLLER_TARGET_LABELS[t]),
      Math.max(0, (CONTROLLER_TARGET_VALUES as readonly string[]).indexOf(controller.target)), (n) =>
        host.project.setSection(CONTROLLER_SECTION_KEY, { target: CONTROLLER_TARGET_VALUES[n] })),
  ];
}

/** The instance menu's entry: `[]` on a host with no facet, and on one where no surface is here (then Settings > MIDI
 *  is the way in). */
const launchpadInstanceMenu: MenuBuilder = {
  placement: "instance.devices",
  build(host) {
    const cfg = getLaunchpadConfig(host.config, host.devices.rpc);
    if (!cfg || !launchpadDetected(cfg)) return [];
    return [submenu("inst-launchpad", "Launchpad", launchpadMenuChildren(host, cfg))];
  },
};

/** The read-only line under the scan row: what it is doing, or what the last one concluded.
 *
 *  A finished scan outranks the recorded device, and reads its own replies rather than waiting for them to
 *  be applied. Both halves matter: re-scanning after unplugging something has to be able to say "nothing
 *  answered" instead of going on naming what used to be there, and the verdict must not flicker through
 *  "nothing" while the apply edge lands a frame later.
 *
 *  `skipped` is worth showing rather than swallowing. A MIDI input is exclusive on Windows, so a port
 *  another application is holding simply cannot be listened on - and "nothing answered" then means
 *  something quite different from "nothing is there". */
function launchpadScanLabel(scan: LaunchpadScanStatus | null, cfg: LaunchpadConfig | null): string {
  if (scan?.busy) return scan.phase || "Scanning...";
  if (scan?.cancelled) return "Cancelled";
  if (scan?.done) {
    // A confirmed find leads even if something went wrong afterwards (a port that would not close): it was applied, so
    // the row has to say so.
    const hit = scan.found;
    if (hit) return `${hit.name} on ${hit.input}`;
    // "Could not look" stays attached to whatever else is said: on Windows a port another program holds cannot be
    // probed at all, and the device being looked for may be behind exactly that port.
    const inUse = scan.skipped > 0 ? ` (${scan.skipped} port(s) in use elsewhere)` : "";
    if (scan.error) return `Error: ${scan.error}${inUse}`;
    if (scan.unsupported) return `${scan.unsupported} - not supported${inUse}`;
    return `Nothing answered${inUse}`;
  }
  if (cfg?.deviceLabel && cfg.selectedInput) return `${cfg.deviceLabel} on ${cfg.selectedInput}`;
  return "Not scanned";
}

/** Settings > MIDI's control-surface rows: the one place a Launchpad can be FOUND, which is why they live here and not
 *  in the instance menu's submenu (that one only appears once a device is known, so a TRS-attached surface would have
 *  nowhere to be set up from). Settings is where you describe your rig; the instance menu is where you play the
 *  hardware you have. */
const launchpadScanRows: MenuBuilder = {
  placement: "settings.midi",
  build(host) {
    const rpc = host.devices.rpc;
    if (!hasLaunchpad(rpc)) return [];
    const lpcfg = getLaunchpadConfig(host.config, rpc);
    const scan = getLaunchpadScan(rpc);
    return [
      separator("midi-sep-surface"),
      // Cancel stays reachable whatever the link holds: it is the only way to stop a scan, and a pair claimed
      // behind one (a restore landing mid-scan) would otherwise leave it running with nothing to press.
      action("midi-scan", scan?.busy ? "Cancel Control Surface Scan" : "Scan for Control Surface",
        () => void (scan?.busy ? cancelLaunchpadScan() : startLaunchpadScan(rpc)),
        !!lpcfg?.connected && !scan?.busy),
      infoRow("midi-scan-status", `Control Surface: ${launchpadScanLabel(scan, lpcfg)}`),
      // Greyed while a scan runs as well: its answer is about to become the selection, and forgetLaunchpad refuses
      // mid-scan (a Forget that did nothing would read as a broken row).
      action("midi-scan-forget", "Forget Control Surface", () => forgetLaunchpad(host.config, rpc),
        !lpcfg?.selectedInput || !!lpcfg?.connected || !!scan?.busy),
    ];
  },
};

// The service's own latches: the scan version it last painted (a repaint only when it moves) and the finishing EDGE
// it has taken (a finished scan is applied once; `busy` re-arms it). `start` puts them back, so a remount re-takes an
// already finished scan's finishing edge and repaints once, which the applied-scan guard makes a no-op for the config
// it would write.
let seenVersion = -1;
let settled = false;

/** Where a mount starts the latches: no version painted and no finishing edge taken, so the next poll applies a scan
 *  that is already finished; a test that resets the scan state goes through this too. */
function resetServiceLatches(): void {
  seenVersion = -1;
  settled = false;
}

onResetLaunchpadScanForTest(resetServiceLatches);

/** One frame-poll tick: step a running scan, take a finished one's answer once, and let the device go when the open
 *  project stops wanting it. True = repaint (the scan moved). */
function pollLaunchpad(host: UiHost): boolean {
  const rpc = host.devices.rpc;
  const scan = pollLaunchpadScan(rpc);
  let repaint = false;
  if (scan) {
    if (scan.busy) settled = false; // a fresh run: its result has not been taken yet
    if (scan.done && !scan.busy && !settled) {
      settled = true;
      applyLaunchpadScan(host.config, scan, rpc);
    }
    if (scan.version !== seenVersion) {
      seenVersion = scan.version;
      repaint = true;
    }
  }
  // And whether the open project still runs the controller: when it stops, the device is let go (see
  // pollControllerRole) rather than left locked in Programmer mode with nothing driving it.
  pollControllerRole(host.project.section<ControllerSection>(CONTROLLER_SECTION_KEY).enabled, rpc);
  return repaint;
}

const launchpadService: UiService = {
  id: "launchpad",
  // Reclaim the control surface config.json left enabled, once the UI is up, and tell the host it is done: a standalone
  // holds its MIDI stream and audio back until that word arrives (rp_device_host::restored), so a restored device's
  // pair is reserved before the shared stream ever opens it and costs no audio restart. Inert where no facet is
  // mounted.
  start: (host) => {
    resetServiceLatches();
    // The automation hook (`__rp_scanControlSurface`) runs outside React, so a mount records its channel for it:
    // the hook then needs no menu to have read the facet first.
    setLaunchpadMountRpc(host.devices.rpc);
    restoreLaunchpad(host.config, host.devices.rpc);
  },
  subscribe: subscribeLaunchpad,
  poll: { frames: 3, run: pollLaunchpad },
};

// Everything the module offers the shell and the tests: the port/scan seam (`./launchpadDevices`) is the contract a menu
// row, a service and a test all read, so it stays published from the package's one UI entry.
export * from "./launchpadDevices";

export default defineUi({
  menus: [launchpadInstanceMenu, launchpadScanRows],
  services: [launchpadService],
});
