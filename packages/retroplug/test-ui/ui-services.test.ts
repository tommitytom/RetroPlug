// The contributed UI services through the real app (docs/modular-graph-phase-f4b-design.md sections 4.5 and 7.5 item
// 7): what only a display can show is that a frame becomes a poll and a poll's "moved" becomes a repaint, and that a
// mount runs the startup work once. The services' own model is pinned in test/services.test.ts; here the App runs them.
//
// The devices are faked at the packs' own clients, as the menu tests fake them: a Launchpad that answers a scan and
// counts its restores, and an N8 link whose status the test moves. No hardware is touched.
import { test, expect, ui, navTo, Key } from "ui-harness";
import type { LaunchpadClient } from "@retroplug/launchpad/client";
import type { N8LinkClient } from "@retroplug/n8";

// Settings > MIDI (where a control surface is found) and Settings > Audio are standalone-only, so the host says it is
// one; the MIDI lists are fixed so both submenus render with no device.
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;
g.__rp_getMidiConfig = () => ({ inputs: [], outputs: [], selectedInput: "", selectedOutput: "" });

const labelOf = (prefix: string) => ui.findByTextContaining(prefix)?.text ?? "(missing)";

// The Launchpad's scan state machine is driven by the wall clock, so the test owns it: nothing finishes until the test
// says the window has passed.
const clock = { ms: 0 };

/** A Launchpad that answers nothing and counts what a mount does to it. */
function fakeLaunchpad() {
  const counts = { claims: 0, restored: 0, opens: 0 };
  const open = new Map<number, { input: boolean; name: string }>();
  let next = 1;
  const client = {
    ports: () => ({ inputs: ["LPProMK3 MIDI"], outputs: ["LPProMK3 MIDI"] }),
    status: () => ({
      connected: counts.claims > 0, generation: counts.claims > 0 ? 1 : 0, sent: 0, dropped: 0,
      input: counts.claims > 0 ? "LPProMK3 MIDI" : "", output: counts.claims > 0 ? "LPProMK3 MIDI" : "", error: "",
    }),
    claim: () => {
      counts.claims++;
      return { connected: true, generation: 1, sent: 0, dropped: 0, input: "LPProMK3 MIDI",
        output: "LPProMK3 MIDI", error: "" };
    },
    release: () => ({ connected: false, generation: 0, sent: 0, dropped: 0, input: "", output: "", error: "" }),
    send: () => true,
    restored: () => {
      counts.restored++;
      return true;
    },
    openInputs: (names: string[]) => {
      counts.opens++;
      return names.map((name) => (open.set(next, { input: true, name }), next++));
    },
    openOutput: (name: string) => {
      counts.opens++;
      return (open.set(next, { input: false, name }), next++);
    },
    sendOn: () => true,
    read: () => ({ messages: [], dropped: 0 }),
    close: (handle: number) => open.delete(handle),
    portError: () => "",
  };
  return { client: client as unknown as LaunchpadClient, counts };
}
const launchpad = fakeLaunchpad();

/** The N8's link, faked: one attached port, off until the menu's Connect row turns it on. */
const n8State = { enabled: false, connected: false, selectedPort: "" };
const n8Link: N8LinkClient = {
  linkConfig: () => ({
    ports: [{ port: "/dev/ttyACM0", isN8: true }],
    selectedPort: n8State.selectedPort,
    connected: n8State.connected,
    enabled: n8State.enabled,
    lookaheadMs: 10,
    bytes: 0,
    error: "",
  }),
  setPort: (port: string) => {
    n8State.selectedPort = port;
  },
  connect: (enabled: boolean) => {
    n8State.enabled = enabled;
  },
  setLookahead: () => {},
};

// The App is a prebuilt bundle with its own copies of the packs' modules, so the only thing it shares with this test is
// globalThis: the packs' test seams read these, and nothing in production sets them.
Object.assign(g, {
  __rp_launchpadClient: launchpad.client,
  __rp_launchpadClock: () => clock.ms,
  __rp_n8LinkClient: n8Link,
});

test("a control-surface scan advances and repaints through frames once its window passes", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30); // settle the mount and run the startup restore
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("MIDI")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);

  clock.ms = 0;
  expect(navTo("Scan for Control Surface")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(1);
  // The scan's poll runs every 3 frames; one frame in, the row still reads what starting published.
  expect(labelOf("Control Surface:").includes("Nothing answered")).toBe(false);
  // The scan's own window: each step is one poll period's worth of frames, with the clock moved on so the window can
  // close. THE CADENCES are pinned by services.test.ts's divisors; this case holds that through frames a poll advances
  // the scan and its finishing edge repaints the row.
  for (let i = 0; i < 4 && !labelOf("Control Surface:").includes("Nothing answered"); i++) {
    clock.ms += 1000;
    ui.pump(3);
  }
  expect(labelOf("Control Surface:")).toBe("Control Surface: Nothing answered");
});

test("the startup restore runs once per mount, and a remount runs it again", () => {
  expect(ui.boot()).toBeTruthy();
  // A mount whose start sees the fake, counted from zero whatever the App's own first mount did.
  launchpad.counts.restored = 0;
  ui.reopen();
  ui.pump(10);
  const afterMount = launchpad.counts.restored;
  expect(afterMount).toBe(1); // the mount's start, and nothing else
  ui.pump(20); // frames pass: the effect must not run again
  expect(launchpad.counts.restored).toBe(afterMount);
  ui.reopen(); // unmount + remount
  ui.pump(10);
  expect(launchpad.counts.restored).toBe(afterMount + 1);
  ui.pump(10);
  expect(launchpad.counts.restored).toBe(afterMount + 1);
});

test("a native N8 change repaints the menu through the service's subscription", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(ui.romDir() + "/bliptoaster.nes", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  ui.tapKey(Key.Esc); // the instance menu
  ui.pump(10);
  expect(navTo("N8 Pro")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(labelOf("Status:")).toBe("Status: Off");
  // The Port cycler: stepping it applies through the link and emits, and the menu repaints off the subscription without
  // a frame (a poll would not have run in two pumps; the label is read off the new config).
  expect(navTo("Port")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(2);
  expect(labelOf("Port:")).toBe("Port: /dev/ttyACM0");
});
