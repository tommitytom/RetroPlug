// The device services (docs/modular-graph-phase-f4b-design.md sections 4.5 and 7.5 item 7): the N8 Pro's card row, the
// Launchpad's scan and its startup restore, driven the way the UI drives them but with no display and no hardware - the
// menu tests' own seam (a MockBackend store graph and the packs' fake clients).
//
// What is pinned is what a service IS, because that is all `useUiServices` reads: the frame cadence, whether a poll
// answers "repaint" (a version it last saw moving, never a steady state), and the startup call a restore makes once.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../testing/mockBackend";
import { composeAppStores, type AppStores } from "../src/appStores";
import { createUiHost, type UiHost } from "../ui/host/uiHost";
import { uiServices } from "../ui/host/menuContributions";
import type { UiHost as AnyUiHost, UiService } from "@retroplug/ui/contribution";
import { tickServices } from "../ui/app/serviceTicker";
import { getN8CartJob, n8RestoreSram, projectCart, resetN8CartJob, setN8LinkClient } from "@retroplug/n8/ui";
import {
  resetLaunchpadScanForTest, setLaunchpadClient, setLaunchpadScanClock, startLaunchpadScan,
} from "@retroplug/launchpad/ui";
import type { LaunchpadClient } from "@retroplug/launchpad/client";
import { nesRom } from "./systems/fixtures";

/** A full UI host over a mock store graph, with a system that drives a cart. */
function rig(): { stores: AppStores; host: UiHost } {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.nes", nesRom());
  stores.project.systems.addSystem("/roms/a.nes");
  const host = createUiHost(stores, { views: { open: () => {} }, beginImport: () => {} });
  return { stores, host };
}

function service(id: string): UiService {
  const found = uiServices().find((each) => each.id === id);
  if (!found) throw new Error(`no ${id} service is contributed`);
  return found;
}

/** A fake Launchpad facet, the client's own interface, with the write counts a startup restore leaves. */
function fakeLaunchpad(connected: boolean) {
  const status = {
    connected, generation: connected ? 1 : 0, sent: 0, dropped: 0,
    input: connected ? "LPProMK3 MIDI" : "", output: connected ? "LPProMK3 MIDI" : "", error: "",
  };
  const counts = { claims: 0, releases: 0, restored: 0 };
  const open = new Map<number, { input: boolean; name: string }>();
  let next = 1;
  const client = {
    ports: () => ({ inputs: ["LPProMK3 MIDI"], outputs: ["LPProMK3 MIDI"] }),
    status: () => ({ ...status, connected: counts.claims > 0 || connected }),
    claim: () => {
      counts.claims++;
      return { ...status, connected: true, generation: 1 };
    },
    release: () => {
      counts.releases++;
      return { ...status, connected: false, generation: 0 };
    },
    send: () => true,
    restored: () => {
      counts.restored++;
      return true;
    },
    openInputs: (names: string[]) => names.map((name) => (open.set(next, { input: true, name }), next++)),
    openOutput: (name: string) => (open.set(next, { input: false, name }), next++),
    sendOn: () => true,
    read: () => ({ messages: [], dropped: 0 }),
    close: (handle: number) => open.delete(handle),
    portError: () => "",
  };
  return { client: client as unknown as LaunchpadClient, counts };
}

test("the product contributes a card-row service and a control-surface service at their own cadences", () => {
  // The two watchers this stage replaced ran at ~10 Hz and ~20 Hz; a service keeps its own frame divisor, so one
  // device's cadence cannot drift with the other's.
  expect(service("n8").poll!.frames).toBe(6);
  expect(service("launchpad").poll!.frames).toBe(3);
  expect(typeof service("launchpad").start).toBe("function");
});

test("the N8 card poll repaints only when the job's snapshot moves, and a job does move it", () => {
  const { host } = rig();
  setN8LinkClient({
    linkConfig: () => ({ ports: [], selectedPort: "", connected: false, enabled: false, lookaheadMs: 10, bytes: 0, error: "" }),
    setPort: () => {},
    connect: () => {},
    setLookahead: () => {},
  });
  try {
    const n8 = service("n8");
    resetN8CartJob(); // a fresh snapshot, whose version bump is the first thing a poll sees
    expect(n8.poll!.run(host)).toBe(true);
    // The steady state is a read and no repaint: the snapshot the row shows has not moved.
    expect(n8.poll!.run(host)).toBe(false);
    expect(n8.poll!.run(host)).toBe(false);
    // A card operation that cannot read its file still ends, and the row must repaint to show its error.
    n8RestoreSram(host, projectCart(host)!, "/saves/missing.srm");
    expect(getN8CartJob().error).toBe("cannot read /saves/missing.srm");
    expect(n8.poll!.run(host)).toBe(true);
    expect(n8.poll!.run(host)).toBe(false);
    // A remount starts from the latch's initial state (the service's start puts it back, as the old per-mount ref
    // began), so the first poll after one repaints once even though the snapshot has not moved.
    n8.start!(host);
    expect(n8.poll!.run(host)).toBe(true);
    expect(n8.poll!.run(host)).toBe(false);
  } finally {
    setN8LinkClient(undefined);
    resetN8CartJob();
  }
});

test("the control-surface service claims the recorded pair at startup and says the word once", () => {
  const { stores, host } = rig();
  const facet = fakeLaunchpad(false);
  setLaunchpadClient(facet.client);
  stores.userConfig.setControlSurface({
    enabled: true, input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", device: "Launchpad Pro [MK3]",
  });
  try {
    const launchpad = service("launchpad");
    launchpad.start!(host);
    expect(facet.counts.claims).toBe(1); // the saved pair, taken
    // The host holds its own MIDI stream and audio back until it hears this, so the restore sends it whatever happened.
    expect(facet.counts.restored).toBe(1);
  } finally {
    setLaunchpadClient(undefined);
    resetLaunchpadScanForTest();
  }
});

test("the control-surface poll steps a scan and repaints on its news, then settles to no repaint", () => {
  const { host } = rig();
  const facet = fakeLaunchpad(false);
  setLaunchpadClient(facet.client);
  resetLaunchpadScanForTest();
  setLaunchpadScanClock(() => 0); // a frozen clock: a probe's answer window never closes, so the scan stays in flight
  try {
    const launchpad = service("launchpad");
    expect(startLaunchpadScan()).toBe(true);
    // The first poll sees the version the scan published when it started (the service's latch starts empty), and every
    // tick that changes nothing answers "no repaint" - which is what keeps the menu off the render path while a scan
    // waits out a window.
    expect(launchpad.poll!.run(host)).toBe(true);
    expect(launchpad.poll!.run(host)).toBe(false);
    expect(launchpad.poll!.run(host)).toBe(false);
    // A remount puts the latches back where a mount finds them, so its first poll repaints once more (the old hooks'
    // refs did the same) and takes the finishing edge again, which the applied-scan guard makes a no-op.
    launchpad.start!(host);
    expect(launchpad.poll!.run(host)).toBe(true);
    expect(launchpad.poll!.run(host)).toBe(false);
  } finally {
    setLaunchpadClient(undefined);
    resetLaunchpadScanForTest();
  }
});

test("the frame ticker gives each service its own cadence and answers one repaint per frame", () => {
  // What `useUiServices` does with a frame, without a display: a counter per service, so a 3-frame device and a
  // 6-frame one keep their own cadences, and one answer for the frame (the hook repaints once, not once per service).
  const runs: string[] = [];
  const poll = (id: string, frames: number, moved: boolean): UiService => ({
    id,
    poll: { frames, run: () => (runs.push(id), moved) },
  });
  const services = [poll("three", 3, true), poll("six", 6, false), { id: "none" } as UiService];
  const ticks = services.map(() => 0);
  const host = {} as AnyUiHost;
  for (let frame = 1; frame <= 12; frame++) {
    const moved = tickServices(services, host, ticks);
    if (frame === 3 || frame === 6 || frame === 9 || frame === 12) expect(moved, `frame ${frame}`).toBe(true);
    else expect(moved, `frame ${frame}`).toBe(false);
  }
  // The three-frame service runs at 3, 6, 9 and 12; the six-frame one at 6 and 12 only.
  const counts = runs.reduce<Record<string, number>>((all, id) => ((all[id] = (all[id] ?? 0) + 1), all), {});
  expect(counts).toEqual({ three: 4, six: 2 });
  // A list that changed length re-sizes its counters rather than reusing another service's.
  const one = [poll("late", 2, false)];
  expect(tickServices(one, host, ticks)).toBe(false);
  expect(ticks.length).toBe(1);
  expect(ticks[0]).toBe(1);
  expect(tickServices(one, host, ticks)).toBe(false);
  expect(ticks[0]).toBe(0);
});
