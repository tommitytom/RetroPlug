// The Launchpad submenu, Settings > MIDI's scan, and the facet under both (launchpadDevices.ts).
//
// The gating is the interesting part. Like the N8 Pro submenu beside it, this one appears only when the hardware is
// actually there - but a Launchpad cannot be detected the way an N8 is. An N8 is a USB cart with a VID:PID and no other
// way to attach it; a Launchpad also speaks TRS/DIN, arriving through an ordinary MIDI interface on a port named after
// the INTERFACE, and nothing in that name says "Launchpad". Nor is there anything to listen for: DIN has no enumeration
// and no idle heartbeat. So presence means one of three things, all pinned below - a port named like the device's
// own USB interface, a live link, or a recorded pick whose BOTH ports are still enumerated. That last one is what a
// Settings > MIDI scan writes, and the reason the scan lives in Settings: it is reachable when this submenu is not.
//
// Native is the pack's FACET (packages/launchpad: rp_launchpad_provider and LaunchpadRpc): it lists ports, claims a
// pair, opens loose ports, and replays the bytes it was handed when it lets a device go. The selection lives in
// config.json and the scan is a TS state machine, so the fake here is the pack's CLIENT (createLaunchpadClient's
// interface) - the cases assert on what TS decided to do through it: which pair it claimed, with which close bytes,
// what it wrote while scanning, and what it persisted.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "@retroplug/ui/menu";
import { PRO_MK3, deviceInquiry, exitToLiveMode, modelQuery } from "@retroplug/launchpad";
import {
  type LaunchpadClient, type LaunchpadPorts, type LaunchpadRead, type LaunchpadStatus,
} from "@retroplug/launchpad/client";
import {
  RELEASE_DELAY_POLLS, SCAN_WINDOW_MS, applyLaunchpadScan, connectLaunchpad, forgetLaunchpad, getLaunchpadConfig,
  looksLikeLaunchpad, pollControllerRole, pollLaunchpadScan, resetLaunchpadScanForTest, restoreLaunchpad,
  setLaunchpadClient, setLaunchpadPorts, setLaunchpadScanClock,
} from "@retroplug/launchpad/ui";
import type { ControlSurfaceSettings } from "@retroplug/launchpad/ui";
import { FakeMidiDevice } from "@retroplug/midi/testSupport/fakeDevice";
import { controllerApps } from "@retroplug/launchpad/registry";
import { type ControllerSection } from "@retroplug/launchpad/projectSection";
import { boolField, clampedInt } from "@retroplug/core/configFields";
import { z } from "zod";
import * as R from "@retroplug/lsdj/codec/regions";
import { gbRom, lsdjRom, nesRom } from "../systems/fixtures";

/** The project section the Launchpad pack contributes: a test reads it through the store's typed read, as a menu
 *  does. */
const CONTROLLER_SECTION = "controller";
const sectionOf = (stores: AppStores): ControllerSection => stores.project.section(CONTROLLER_SECTION);

function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    beginSongImport: () => {},
  };
}

function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}
const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);

/** A Pro MK3's Device Inquiry answer: F0 7E <dev> 06 02 <Novation> <family 13 01> 00 00 <version> F7. */
const PRO_MK3_REPLY = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0, 0, 0, 1, 2, 3, 0xf7];

// A machine with a Pro MK3 on USB, an unrelated keyboard, and a MIDI interface - the three cases the port cyclers
// have to handle at once.
const INPUTS = ["Arturia KeyStep 32", "LPProMK3 MIDI", "MIDISPORT 2x2 Port A"];
const OUTPUTS = ["LPProMK3 MIDI", "MIDISPORT 2x2 Port A"];

const same = (a: readonly number[], b: readonly number[]): boolean =>
  a.length === b.length && a.every((v, i) => v === b[i]);

/** The pack's facet, faked at its client: real enough to hold state, so a sequence of menu actions reads true. Its
 *  claim
 *  path is the facet's own order - the old pair let go first (its close bytes replayed), the ports checked against the
 *  list, then the open - so a refusal behaves as LaunchpadRpc's does. */
class FakeFacet implements LaunchpadClient {
  inputs = INPUTS.slice();
  outputs = OUTPUTS.slice();
  held: { input: string; output: string; closeBytes: number[] } | null = null;
  claims: { input: string; output: string; closeBytes: number[] }[] = [];
  released: number[][] = []; // the close bytes replayed at each release, in order
  releaseCalls = 0; // every release(), including one that holds nothing to replay
  away = new Set<string>(); // ports listed but that will not open (a device unplugged, a cable pulled)
  dropped = 0;
  error = "";
  restoredCalls = 0;
  sentToDevice: number[][] = []; // send() while a pair is held
  inputBatches = 0; // openInputs calls

  // loose ports (the scan)
  // output -> who answers the inquiry, on which input. `isPro` says whether it ALSO answers the Pro MK3's own model
  // query - true by default for a reply carrying the Pro's family bytes, since that is a real Pro; set it false to
  // play a Mini MK3, which answers the inquiry with the very same bytes but ignores the Pro header.
  answers = new Map<string, { input: string; bytes: number[]; isPro?: boolean }>();
  busyPorts = new Set<string>(); // ports another application holds
  written: { port: string; bytes: number[] }[] = [];
  private nextHandle = 1;
  private open = new Map<number, { input: boolean; name: string; queue: number[][] }>();

  /** What is held right now (a method, so a case that cleared `held` by hand is not narrowed to null). */
  current(): FakeFacet["held"] {
    return this.held;
  }

  openHandles(): number {
    return this.open.size;
  }

  ports(): LaunchpadPorts {
    return { inputs: this.inputs.slice(), outputs: this.outputs.slice() };
  }

  status(): LaunchpadStatus {
    const held = this.held;
    return {
      connected: !!held,
      generation: held ? 1 : 0,
      sent: 0,
      dropped: this.dropped,
      input: held?.input ?? "",
      output: held?.output ?? "",
      error: this.error,
    };
  }

  claim(input: string, output: string, closeBytes: Uint8Array): LaunchpadStatus {
    this.release(); // the facet lets the OLD pair go first, its close bytes out
    const fail = (why: string): LaunchpadStatus => {
      this.error = why;
      return this.status();
    };
    if (input === "" || output === "") return fail("no port chosen");
    if (!this.inputs.includes(input)) return fail(`not connected: ${input}`);
    if (!this.outputs.includes(output)) return fail(`not connected: ${output}`);
    // The real backend's words (midi/RtMidiBackend.cpp's port classes), in ITS order: the claim opens the output first,
    // so a
    // pair whose device is away is refused by the output.
    if (this.away.has(output)) return fail(`MIDI output port not found: ${output}`);
    if (this.away.has(input)) return fail(`MIDI input port not found: ${input}`);
    this.error = "";
    this.held = { input, output, closeBytes: Array.from(closeBytes) };
    this.claims.push(this.held);
    return this.status();
  }

  release(): LaunchpadStatus {
    this.releaseCalls++;
    this.releaseHeld();
    this.error = "";
    return this.status();
  }

  send(bytes: Uint8Array): boolean {
    if (!this.held) return false;
    this.sentToDevice.push(Array.from(bytes));
    return true;
  }

  restored(): boolean {
    this.restoredCalls++;
    return true;
  }

  openInputs(names: string[]): number[] {
    this.inputBatches++;
    return names.map((n) => this.openPort(true, n));
  }
  openOutput(name: string): number {
    return this.openPort(false, name);
  }
  sendOn(handle: number, bytes: Uint8Array): boolean {
    const p = this.open.get(handle);
    if (!p || p.input) return false;
    const data = Array.from(bytes);
    this.written.push({ port: p.name, bytes: data });
    const a = this.answers.get(p.name);
    if (!a) return true;
    let reply: number[] | null = null;
    if (same(data, deviceInquiry())) reply = a.bytes;
    else if (same(data, modelQuery(PRO_MK3)) && (a.isPro ?? (a.bytes[8] === 0x13 && a.bytes[9] === 0x01)))
      reply = [0xf0, 0x00, 0x20, 0x29, 0x02, 0x0e, 0x00, 0x00, 0x00, 0xf7]; // "showing layout 0"
    if (reply) for (const q of this.open.values()) if (q.input && q.name === a.input) q.queue.push([...reply]);
    return true;
  }
  read(handle: number): LaunchpadRead {
    const p = this.open.get(handle);
    if (!p) return { messages: [], dropped: 0 };
    const messages = p.queue.map((m) => Uint8Array.from(m));
    p.queue = [];
    return { messages, dropped: 0 };
  }
  close(handle: number): boolean {
    return this.open.delete(handle);
  }
  portError(): string {
    return "";
  }

  /** Deliver a message on an input, as if the device sent it unprompted. */
  hear(input: string, bytes: number[]): void {
    for (const q of this.open.values()) if (q.input && q.name === input) q.queue.push([...bytes]);
  }

  /** A send on a pool handle, watched: the hook the echo case needs (a soft-thru answers a query with itself). */
  sending(hook: (handle: number, bytes: number[]) => void): LaunchpadClient {
    const self = this;
    return {
      ports: () => self.ports(), status: () => self.status(),
      claim: (a, b, c) => self.claim(a, b, c), release: () => self.release(), send: (b) => self.send(b),
      restored: () => self.restored(),
      openInputs: (n) => self.openInputs(n), openOutput: (n) => self.openOutput(n),
      sendOn: (h, b) => { hook(h, Array.from(b)); return self.sendOn(h, b); },
      read: (h) => self.read(h), close: (h) => self.close(h), portError: () => self.portError(),
    };
  }

  private openPort(input: boolean, name: string): number {
    if (this.busyPorts.has(name) || this.away.has(name)) return 0;
    const h = this.nextHandle++;
    this.open.set(h, { input, name, queue: [] });
    return h;
  }

  private releaseHeld(): void {
    if (!this.held) return;
    this.released.push(this.held.closeBytes);
    this.held = null;
  }
}

/** Run `body` against a fresh fake facet, fresh stores, and a stopped clock the case advances by hand. */
function withHost(
  setup: (facet: FakeFacet) => void,
  body: (facet: FakeFacet, stores: AppStores, be: MockBackend, clock: { ms: number }) => void,
  selection: Partial<ControlSurfaceSettings> = {},
): void {
  const facet = new FakeFacet();
  setup(facet);
  setLaunchpadClient(facet);
  const clock = { ms: 1000 };
  setLaunchpadScanClock(() => clock.ms);
  try {
    const be = new MockBackend();
    const stores = composeAppStores({ backend: be });
    stores.userConfig.setSection("controlSurface", selection);
    body(facet, stores, be, clock);
  } finally {
    setLaunchpadClient(undefined);
    resetLaunchpadScanForTest();
  }
}

function withoutSeam(body: () => void): void {
  setLaunchpadClient(null);
  try {
    body();
  } finally {
    setLaunchpadClient(undefined);
  }
}

/** Seed a plain NES cart (no tracker role, so the block holds Launchpad alone), then the submenu's rows. */
function submenu(stores: AppStores, be: MockBackend): MenuItem[] {
  if (stores.project.systems.view().length === 0) {
    be.seed("/roms/a.nes", nesRom());
    stores.project.systems.addSystem("/roms/a.nes");
  }
  const sys = stores.project.systems.view()[0];
  return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-launchpad");
}

/** Settings > MIDI's rows, which is where a control surface is FOUND. Needs the standalone flags the real host sets,
 *  since the whole MIDI submenu is standalone-only. */
function midiSettingsRows(stores: AppStores, be: MockBackend): MenuItem[] {
  const g = globalThis as Record<string, unknown>;
  const saved = { standalone: g.__rp_isStandalone };
  g.__rp_isStandalone = true;
  // The midi package's stream is what the container's own rows come from; the scan rows below it are the pack's.
  const midi = new FakeMidiDevice({ inputs: [], outputs: [] }).install();
  try {
    if (stores.project.systems.view().length === 0) {
      be.seed("/roms/a.nes", nesRom());
      stores.project.systems.addSystem("/roms/a.nes");
    }
    const sys = stores.project.systems.view()[0];
    const items = buildInstanceMenu({ ...ctxOf(stores), system: sys }).items;
    return submenuChildren(submenuChildren(items, "inst-settings"), "set-midi");
  } finally {
    midi.uninstall();
    g.__rp_isStandalone = saved.standalone;
  }
}

/** Whether the instance menu shows the Launchpad submenu, for this rig + recorded selection. */
function submenuShown(setup: (h: FakeFacet) => void, selection: Partial<ControlSurfaceSettings> = {}): boolean {
  let shown = false;
  withHost(setup, (_f, stores, be) => {
    shown = submenu(stores, be).length > 0;
  }, selection);
  return shown;
}

/** Step a running scan until it settles, advancing the clock a frame at a time. */
function runScan(clock: { ms: number }): ReturnType<typeof pollLaunchpadScan> {
  for (let i = 0; i < 1000; i++) {
    const s = pollLaunchpadScan();
    if (!s || !s.busy) return s;
    clock.ms += 16;
  }
  throw new Error("scan never finished");
}

const TRS = {
  inputs: ["Arturia KeyStep 32", "MIDISPORT 2x2 Port A"],
  outputs: ["MIDISPORT 2x2 Port B"],
};
const TRS_PICK = { input: "MIDISPORT 2x2 Port A", output: "MIDISPORT 2x2 Port B", device: "Launchpad Pro [MK3]" };

// --- presence ---------------------------------------------------------------------------------------------

test("no seam, no submenu", () => {
  withoutSeam(() => {
    const be = new MockBackend();
    expect(submenu(composeAppStores({ backend: be }), be).length).toBe(0);
  });
});

test("nothing plugged in, no submenu", () => {
  // A machine with no control surface does not carry a row for one.
  expect(submenuShown((h) => { h.inputs = []; h.outputs = []; })).toBe(false);
  // Neither does a machine with plenty of MIDI gear that is simply not a Launchpad.
  expect(submenuShown((h) => { h.inputs = ["Arturia KeyStep 32"]; h.outputs = ["Arturia KeyStep 32"]; })).toBe(false);
});

test("a Launchpad on USB needs no scan - its own port name is enough", () => {
  expect(submenuShown((h) => { h.inputs = ["LPProMK3 MIDI"]; h.outputs = ["LPProMK3 MIDI"]; })).toBe(true);
});

test("a recorded pick speaks for a device no name could identify", () => {
  // The TRS topology, after a scan: the ports belong to somebody's interface and say nothing about what is plugged into
  // them, so the recorded pair is the only evidence there is.
  expect(submenuShown((h) => Object.assign(h, TRS), TRS_PICK)).toBe(true);
  // Presence is checked, not trusted: unplug that interface and the row goes with it, exactly as the N8 Pro submenu
  // next door behaves. Settings > MIDI still has the scan, so there is a way back.
  expect(submenuShown((h) => { h.inputs = ["Arturia KeyStep 32"]; h.outputs = []; }, TRS_PICK)).toBe(false);
  // Half a pair is not a device either - an output alone cannot be listened to.
  expect(submenuShown((h) => { h.inputs = TRS.inputs; h.outputs = []; }, TRS_PICK)).toBe(false);
});

test("a connected link always shows, whatever the port names look like", () => {
  expect(submenuShown((h) => {
    Object.assign(h, TRS);
    h.held = { input: "MIDISPORT 2x2 Port A", output: "MIDISPORT 2x2 Port B", closeBytes: [] };
  })).toBe(true);
});

// --- picking + claiming -----------------------------------------------------------------------------------

test("the port cycler offers EVERY hardware port, not just ones named like a Launchpad", () => {
  withHost(() => {}, (_h, stores, be) => {
    const input = findItem(submenu(stores, be), "lp-input");
    expect(input?.kind).toBe("cycler");
    expect(input?.label).toBe("Input Device: (none)");
    // Stepping forward from "(none)" reaches the FIRST port in the list, which is nothing like a Launchpad. A filtered
    // list would have skipped it - and would have skipped the interface port a TRS-attached Launchpad actually
    // arrives on.
    input?.onCycle?.(1);
    expect(stores.userConfig.section("controlSurface").input).toBe("Arturia KeyStep 32");
  });
});

test("a hinted port is tagged; an unhinted one is perfectly selectable", () => {
  withHost(() => {}, (_h, stores, be) => {
    expect(findItem(submenu(stores, be), "lp-input")?.label).toBe("Input Device: LPProMK3 MIDI (detected)");
  }, { input: "LPProMK3 MIDI" });
  // The TRS topology: the surface is on an interface port, so no tag - and that is not an error state.
  withHost(() => {}, (_h, stores, be) => {
    expect(findItem(submenu(stores, be), "lp-input")?.label).toBe("Input Device: MIDISPORT 2x2 Port A");
  }, { input: "MIDISPORT 2x2 Port A" });
});

test("picking a port stores the port's name, with the display tag stripped back off", () => {
  // Cycling forward from the KeyStep lands on the Launchpad, whose ROW READS "LPProMK3 MIDI (detected)". The name
  // stored has to be the port's, not the label's, or it would name a port that does not exist.
  withHost(() => {}, (h, stores, be) => {
    findItem(submenu(stores, be), "lp-input")?.onCycle?.(1);
    expect(stores.userConfig.section("controlSurface").input).toBe("LPProMK3 MIDI");
    expect(h.claims).toEqual([]); // not connected, so a pick is only a pick
  }, { input: "Arturia KeyStep 32", output: "LPProMK3 MIDI" });
});

test("Connect with nothing chosen resolves the hinted default first, and claims it", () => {
  withHost(() => {}, (h, stores, be) => {
    findItem(submenu(stores, be), "lp-connect")?.onSelect?.();
    expect(stores.userConfig.section("controlSurface")).toEqual({
      input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true, device: "",
    });
    expect(h.held?.input).toBe("LPProMK3 MIDI");
  });
});

test("Connect with no hinted port at all does not invent one", () => {
  // Driven directly rather than through the row, because the row cannot reach this state any more: with no hint and
  // nothing recorded there is no submenu to press Connect in. The rule still has to hold for the path that does exist -
  // cycling a recorded Input Device back to "(none)" and reconnecting - and the cost of getting it wrong is claiming
  // somebody's keyboard and calling it a Launchpad.
  withHost((h) => { h.inputs = ["Arturia KeyStep 32"]; h.outputs = ["Arturia KeyStep 32"]; }, (h, stores) => {
    connectLaunchpad(stores.userConfig, true, getLaunchpadConfig(stores.userConfig));
    expect(stores.userConfig.section("controlSurface").input).toBe("");
    expect(stores.userConfig.section("controlSurface").enabled).toBe(true);
    expect(h.claims).toEqual([]);
  });
});

test("every claim carries the bytes that release the device", () => {
  // Programmer mode locks the Launchpad's own Settings menu. Native replays these on release AND on destruct, so a
  // claim without them would strand the user's hardware when RetroPlug quits.
  withHost(() => {}, (h, stores, be) => {
    findItem(submenu(stores, be), "lp-connect")?.onSelect?.();
    expect(h.claims.length).toBe(1);
    expect(h.claims[0].closeBytes).toEqual(exitToLiveMode(PRO_MK3));
  });
});

test("Disconnect releases the device and persists the choice", () => {
  withHost(() => {}, (h, stores, be) => {
    restoreLaunchpad(stores.userConfig);
    expect(h.held !== null).toBe(true);
    findItem(submenu(stores, be), "lp-connect")?.onSelect?.(); // reads "Disconnect"
    expect(h.held).toBe(null);
    expect(h.released).toEqual([exitToLiveMode(PRO_MK3)]);
    expect(stores.userConfig.section("controlSurface").enabled).toBe(false);
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("a port change while connected switches live, saying goodbye to the old device first", () => {
  withHost(() => {}, (h, stores, be) => {
    restoreLaunchpad(stores.userConfig);
    findItem(submenu(stores, be), "lp-output")?.onCycle?.(1); // LPProMK3 MIDI -> MIDISPORT 2x2 Port A
    expect(h.claims.length).toBe(2);
    // The old pair is let go FIRST, with its close bytes: a Launchpad stays in Programmer mode with its front panel
    // locked until they arrive, and the new claim is what the facet sends them from.
    expect(h.released.length).toBe(1);
    expect(h.released[0]).toEqual(exitToLiveMode(PRO_MK3));
    expect(h.claims[1]).toEqual({
      input: "LPProMK3 MIDI", output: "MIDISPORT 2x2 Port A", closeBytes: exitToLiveMode(PRO_MK3),
    });
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

// --- persistence + startup --------------------------------------------------------------------------------

test("the selection lives in config.json, so a fresh launch reclaims what was left connected", () => {
  withHost(() => {}, (h, stores, be) => {
    findItem(submenu(stores, be), "lp-connect")?.onSelect?.();
    // Same backend, fresh stores: the next launch. The link is down until the UI restores it.
    h.held = null;
    const next = composeAppStores({ backend: be });
    expect(next.userConfig.section("controlSurface").input).toBe("LPProMK3 MIDI");
    restoreLaunchpad(next.userConfig);
    expect(h.current()?.input).toBe("LPProMK3 MIDI");
    expect(h.current()?.closeBytes).toEqual(exitToLiveMode(PRO_MK3));
  });
});

test("a pair left disconnected is not taken at startup", () => {
  withHost(() => {}, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    expect(h.claims).toEqual([]);
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: false });
});

test("restoring a device that is not here shows why, rather than pretending", () => {
  withHost((h) => h.away.add("LPProMK3 MIDI"), (_h, stores, be) => {
    restoreLaunchpad(stores.userConfig);
    expect(findItem(submenu(stores, be), "lp-status")?.label)
      .toBe("Status: Error: MIDI output port not found: LPProMK3 MIDI");
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("the status row reads what the link is actually doing", () => {
  const cases: [(h: FakeFacet) => void, Partial<ControlSurfaceSettings>, string][] = [
    // Nothing chosen, the feature off, and a Launchpad in the machine's ports: the row points at where a device is
    // configured, which is the whole of what an upgrade from the legacy host needs (design section 5).
    [() => {}, {}, "Status: a Launchpad is here - Connect, or Settings > MIDI to scan for one"],
    // ...and a pair already chosen needs no hint, even with a Launchpad in the machine.
    [() => {}, { input: "Arturia KeyStep 32", output: "Arturia KeyStep 32" }, "Status: not connected"],
    [(h) => { h.held = { input: "a", output: "b", closeBytes: [] }; }, { enabled: true }, "Status: connected"],
    [(h) => { h.held = { input: "a", output: "b", closeBytes: [] }; h.dropped = 3; }, { enabled: true },
     "Status: connected (3 dropped)"],
    [() => {}, { enabled: true }, "Status: pick both ports"],
    [() => {}, { enabled: true, input: "a", output: "b" }, "Status: not connected"],
    // "connected" alone reads the same whether the cable reaches a Launchpad or nothing at all.
    [(h) => { h.held = { input: "a", output: "b", closeBytes: [] }; }, { enabled: true, device: "Launchpad Pro [MK3]" },
     "Status: connected - Launchpad Pro [MK3]"],
  ];
  for (const [setup, selection, label] of cases) {
    withHost(setup, (_h, stores, be) => {
      expect(findItem(submenu(stores, be), "lp-status")?.label).toBe(label);
    }, selection);
  }
});

// --- the app rows: project state --------------------------------------------------------------------------

/** A project whose GB cart is in SYNC `sync` (8 = MidiMap, 9 = Lsdj), the controller enabled and its cart polled: what
 *  the menu's cart-sync row reads (the controller's service state, from the battery). */
function withCart(stores: AppStores, be: MockBackend, sync: number): void {
  // An LSDj cart: the SYNC byte is its working song's, and the poll asks the cart's own playback for it (edge E3), so a
  // ROM whose title names no tracker would read "SYNC unread" whatever the battery holds.
  be.seed("/roms/cart.gb", lsdjRom());
  stores.project.systems.addSystem("/roms/cart.gb");
  const b = new Uint8Array(0x20000);
  b[0x813e] = 0x6a; // 'j'
  b[0x813f] = 0x6b; // 'k': a readable LSDj battery
  b[R.kModernRegions.syncMode] = sync;
  be.setSram(stores.project.systems.view()[0].id, b);
  stores.project.setSection(CONTROLLER_SECTION, { enabled: true, target: "system" });
  stores.project.pollServices();
}

test("the cart-sync row reads the app's own rule, not a mode string", () => {
  // Whether the cart is being driven is the APP's answer (controllerSyncOverride's `driving`: the mode its launches
  // need is the one the cart is put in), never "the mode is not the string off" - a second app may spell its
  // off-mode anything or have none at all, and the row would then claim the cart was driven while nothing was sent.
  withHost(() => {}, (_h, stores, be) => {
    withCart(stores, be, 8); // MidiMap: what MI.MAP's launches need
    expect(findItem(submenu(stores, be), "lp-cart-sync")?.label).toBe("Cart: LSDj MI.MAP - driving it");
  });
  withHost(() => {}, (_h, stores, be) => {
    withCart(stores, be, 1); // the player knocked it to LSDJ (master sync): launches are ignored, and the row says so
    expect(findItem(submenu(stores, be), "lp-cart-sync")?.label)
      .toBe("Cart: LSDj MI.MAP - not driving it: its SYNC is Lsdj");
  });
  withHost(() => {}, (_h, stores, be) => {
    // An LSDj cart with no battery the reader understands: nothing has been polled, so the row says what it does not
    // know rather than guessing that the cart is willing.
    be.seed("/roms/cart.gb", lsdjRom());
    stores.project.systems.addSystem("/roms/cart.gb");
    stores.project.setSection(CONTROLLER_SECTION, { enabled: true, target: "system" });
    stores.project.pollServices();
    expect(findItem(submenu(stores, be), "lp-cart-sync")?.label).toBe("Cart: LSDj MI.MAP - driving it (SYNC unread)");
  });
  withHost(() => {}, (_h, stores, be) => {
    // A ROM that is no tracker's places no sync node, so there is nothing for the controller to drive or to claim.
    be.seed("/roms/plain.gb", gbRom());
    stores.project.systems.addSystem("/roms/plain.gb");
    stores.project.setSection(CONTROLLER_SECTION, { enabled: true, target: "system" });
    stores.project.pollServices();
    expect(findItem(submenu(stores, be), "lp-cart-sync")).toBe(undefined);
  });
});

test("the cart-sync row is not shown for a cart whose sync role is stored but not placed", () => {
  // An mGB ROM that still stores the lsdj-sync it carried before a ROM swap: the graph places mgb, so there is no sync
  // node to put in MidiMap and the row must not claim the cart is driven.
  withHost(() => {}, (_h, stores, be) => {
    be.seed("/roms/mgb.gb", lsdjRom("MGB"));
    stores.project.systems.adopt({
      romPath: "/roms/mgb.gb", core: "sameboy",
      roles: [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1 } }],
    });
    const view = stores.project.systems.view()[0];
    expect(view.roles.some((r) => r.kind === "lsdj-sync")).toBe(true);
    expect(view.placed.includes("lsdj-sync")).toBe(false);
    stores.project.setSection(CONTROLLER_SECTION, { enabled: true, target: "system" });
    stores.project.pollServices();
    expect(findItem(submenu(stores, be), "lp-cart-sync")).toBe(undefined);
  });
});

test("the app rows edit PROJECT settings, and each edit re-pushes the kernel", () => {
  withHost(() => {}, (_h, stores, be) => {
    // A cycler's onCycle takes a DIRECTION, so each of these steps once from its default: off -> on, bar -> beat,
    // follow on -> off, and the emulated cart -> MIDI out.
    const rows = submenu(stores, be);
    let pushes = 0; // counted from HERE: seeding the cart above pushes once on its own
    stores.project.addOnSystemsChange(() => void pushes++);

    findItem(rows, "lp-enabled")?.onCycle?.(1);
    findItem(rows, "lp-app-quantise")?.onCycle?.(-1);
    findItem(rows, "lp-app-follow")?.onCycle?.(-1);
    findItem(rows, "lp-target")?.onCycle?.(1);

    const c = sectionOf(stores);
    expect(c.enabled).toBe(true);
    expect(c.target).toBe("midiOut");
    expect(c.appConfig.quantise).toBe("beat");
    expect(c.appConfig.follow).toBe(false);
    // The controller role is SYNTHESIZED at publication time, so an edit that never reached setSystems would leave the
    // audio thread running the previous settings.
    expect(pushes).toBe(4);
    // ...and none of it is machine state: config.json's selection is untouched.
    expect(stores.userConfig.section("controlSurface").enabled).toBe(false);
  });
});

test("an app knob's row shows the value the app's own parse gives the runtime", () => {
  // A choice the app's schema does not have snaps to the schema's default when the runtime parses the stored config
  // (parseAppConfig). MI.MAP's default is "bar", index 2 of its four values, so a stored value outside the list runs
  // "Next bar" and its row has to show that rather than the first label.
  withHost(() => {}, (_h, stores, be) => {
    stores.project.setSection(CONTROLLER_SECTION, { appConfig: { quantise: "whenever" } });
    expect(findItem(submenu(stores, be), "lp-app-quantise")?.label).toBe("Launches: Next bar");
  });
});

test("a toggle and a range knob read the app's parse too", () => {
  // MI.MAP's knobs are choices and a toggle defaulting true, so its own rows cannot show these two shapes: a toggle
  // whose schema default is false, stored as a non-boolean (the schema snaps it to Off), and a range stored past its
  // top bound. The range's step table reaches past the schema's bound on purpose: the row has to show the clamped
  // value the app runs (10), not the step a literal reading of the stored 999 points at (15).
  const real = controllerApps.get("lsdj-midimap")!;
  controllerApps.register({
    id: "lsdj-midimap",
    label: "Fake App",
    config: z.object({ follow: boolField(false), level: clampedInt(0, 10, 5) }),
    menu: [
      { key: "follow", label: "Follow", toggle: true },
      { key: "level", label: "Level", range: { steps: [0, 5, 10, 15], labels: ["Low", "Mid", "High", "Top"] } },
    ],
  });
  try {
    withHost(() => {}, (_h, stores, be) => {
      stores.project.setSection(CONTROLLER_SECTION, { appConfig: { follow: "yes", level: 999 } });
      const rows = submenu(stores, be);
      expect(findItem(rows, "lp-app-follow")?.label).toBe("Follow: Off");
      expect(findItem(rows, "lp-app-level")?.label).toBe("Level: High");
    });
  } finally {
    controllerApps.register(real);
  }
});

test("one app knob at a time: setting quantise does not wipe follow", () => {
  withHost(() => {}, (_h, stores, be) => {
    submenu(stores, be);
    stores.project.setSection(CONTROLLER_SECTION, { appConfig: { follow: false } });
    stores.project.setSection(CONTROLLER_SECTION, { appConfig: { quantise: "beat" } });
    const c = sectionOf(stores);
    expect(c.appConfig.follow).toBe(false);
    expect(c.appConfig.quantise).toBe("beat");
  });
});

test("switching to a different app starts it from a clean config, not the old app's knobs", () => {
  // Merging kept every key of every app ever chosen in the .rplg, handed to whichever app came next.
  withHost(() => {}, (_h, stores) => {
    stores.project.setSection(CONTROLLER_SECTION, { appConfig: { follow: false, quantise: "beat" } });
    stores.project.setSection(CONTROLLER_SECTION, { app: "some-other-app", appConfig: {} });
    expect(sectionOf(stores).appConfig).toEqual({});
    stores.project.setSection(CONTROLLER_SECTION, { app: "some-other-app", appConfig: { x: 1 } }); // same app: merges as before
    stores.project.setSection(CONTROLLER_SECTION, { appConfig: { y: 2 } });
    expect(sectionOf(stores).appConfig).toEqual({ x: 1, y: 2 });
  });
});

// --- Settings > MIDI: finding one -------------------------------------------------------------------------

test("with nothing answering, the scan writes exactly the Device Inquiry, once per output, and nothing else", () => {
  withHost(() => {}, (h, stores, be, clock) => {
    const rows = midiSettingsRows(stores, be);
    expect(h.written).toEqual([]); // merely opening Settings writes no MIDI at anybody's gear
    findItem(rows, "midi-scan")?.onSelect?.();
    runScan(clock);
    expect(h.written).toEqual(OUTPUTS.map((port) => ({ port, bytes: deviceInquiry() })));
    expect(h.openHandles()).toBe(0); // and every port it opened is closed again
  });
});

test("the scan row becomes Cancel while a scan runs, and steps aside while the link holds the ports", () => {
  withHost(() => {}, (_h, stores, be) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    const row = findItem(midiSettingsRows(stores, be), "midi-scan");
    expect(row?.label).toBe("Cancel Control Surface Scan");
    expect(row?.disabled).toBe(undefined);
  });
  // A pair claimed BEHIND a running scan (a restore landing mid-scan): Cancel is still the only way to stop it, so it
  // must not be greyed by the link's state.
  withHost(() => {}, (h, stores, be) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    h.held = { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", closeBytes: [] };
    const row = findItem(midiSettingsRows(stores, be), "midi-scan");
    expect(row?.label).toBe("Cancel Control Surface Scan");
    expect(row?.disabled).toBe(undefined);
  });
  // Connected: those ports cannot be reopened, and there is nothing left to find.
  withHost((h) => { h.held = { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", closeBytes: [] }; }, (h, stores, be) => {
    const rows = midiSettingsRows(stores, be);
    expect(findItem(rows, "midi-scan")?.disabled).toBe(true);
    expect(findItem(rows, "midi-scan-forget")?.disabled).toBe(true); // nor is this the moment to forget it
    findItem(rows, "midi-scan")?.onSelect?.(); // even pressed anyway, it writes nothing
    expect(h.written).toEqual([]);
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("a Novation answer records the PAIR it came back on, and names the model", () => {
  // The output probed and the input that answered are different ports, which is the whole reason to ask: no amount of
  // listening tells you which input belongs to which output.
  withHost((h) => {
    Object.assign(h, TRS);
    h.answers.set("MIDISPORT 2x2 Port B", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY });
  }, (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    const found = applyLaunchpadScan(stores.userConfig, runScan(clock)!);
    expect(found).toEqual({ input: "MIDISPORT 2x2 Port A", output: "MIDISPORT 2x2 Port B", name: PRO_MK3.name });
    expect(stores.userConfig.section("controlSurface")).toEqual({
      input: TRS_PICK.input, output: TRS_PICK.output, enabled: false, device: TRS_PICK.device,
    });
    expect(submenu(stores, be).length > 0).toBe(true); // which is what makes the submenu appear
  });
});

test("anything that is not a Novation answer is left exactly where it was", () => {
  const cases: number[][] = [
    // Another manufacturer answering the same universal question. It is a valid inquiry reply, and not ours.
    [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x41, 0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0, 0, 0, 0xf7],
    // A keyboard on the same port playing a note into the window.
    [0x90, 60, 100],
    [0xf0, 0x7e], // a truncated reply
  ];
  for (const bytes of cases) {
    withHost((h) => h.answers.set("MIDISPORT 2x2 Port A", { input: "Arturia KeyStep 32", bytes }), (_h, stores, be, clock) => {
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      expect(applyLaunchpadScan(stores.userConfig, runScan(clock)!)).toBe(null);
      expect(stores.userConfig.section("controlSurface").input).toBe(""); // somebody's keyboard was not claimed
    });
  }
});

test("a Novation answer is found among the noise, not blocked by it", () => {
  withHost((h) => {
    h.answers.set("LPProMK3 MIDI", { input: "Arturia KeyStep 32", bytes: [0x90, 60, 100] }); // somebody playing
    h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY });
  }, (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    expect(applyLaunchpadScan(stores.userConfig, runScan(clock)!)?.input).toBe("MIDISPORT 2x2 Port A");
  });
});

test("the scan status row says which of 'nothing there' and 'could not look' happened", () => {
  const label = (stores: AppStores, be: MockBackend) => findItem(midiSettingsRows(stores, be), "midi-scan-status")?.label;
  withHost(() => {}, (_h, stores, be) => {
    expect(label(stores, be)).toBe("Control Surface: Not scanned");
  });
  withHost(() => {}, (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    expect(label(stores, be)).toBe("Control Surface: Probing LPProMK3 MIDI");
    runScan(clock);
    expect(label(stores, be)).toBe("Control Surface: Nothing answered");
  });
  // A MIDI input is exclusive on Windows, so a port another app holds cannot be listened on at all. Saying so is the
  // difference between "you have no Launchpad" and "close the other program and try again".
  withHost((h) => { h.busyPorts.add("Arturia KeyStep 32"); h.busyPorts.add("MIDISPORT 2x2 Port A"); },
    (_h, stores, be, clock) => {
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      runScan(clock);
      // Two inputs plus the interface's output: three ports nobody could look through.
      expect(label(stores, be)).toBe("Control Surface: Nothing answered (3 port(s) in use elsewhere)");
    });
  // A device recorded by an earlier session, with no scan run since.
  withHost(() => {}, (_h, stores, be) => {
    expect(label(stores, be)).toBe("Control Surface: Launchpad Pro [MK3] on MIDISPORT 2x2 Port A");
  }, TRS_PICK);
  // A scan that just answered reports itself, and outranks a stale recording.
  withHost((h) => h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: PRO_MK3_REPLY }), (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    runScan(clock);
    expect(label(stores, be)).toBe("Control Surface: Launchpad Pro [MK3] on LPProMK3 MIDI");
  }, TRS_PICK);
});

test("a reply is credited to the output that was being probed, never the next one", () => {
  // The answer window for each output closes before the next probe goes out, and whatever arrived in it is read first -
  // so a slow device cannot be blamed on the port after it.
  withHost((h) => h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY }),
    (_h, stores, be, clock) => {
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      const s = runScan(clock)!;
      // Every reply - the inquiry answer and the model's answer to the follow-up - is the probed output's.
      expect(s.replies.length > 0 && s.replies.every((r) => r.output === "MIDISPORT 2x2 Port A")).toBe(true);
      expect(clock.ms - 1000 >= SCAN_WINDOW_MS * OUTPUTS.length).toBe(true); // each window ran its course
    });
});

test("a device talking BEFORE it is asked is not taken as an answer", () => {
  withHost(() => {}, (h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    runScan(clock);
    // Arrives after every window has closed: a straggler, not a reply to anything we sent.
    h.hear("LPProMK3 MIDI", PRO_MK3_REPLY);
    expect(pollLaunchpadScan()!.replies).toEqual([]);
  });
});

test("Forget clears the pair AND the name, so the submenu goes away again", () => {
  withHost((h) => Object.assign(h, TRS), (_h, stores, be) => {
    findItem(midiSettingsRows(stores, be), "midi-scan-forget")?.onSelect?.();
    expect(stores.userConfig.section("controlSurface")).toEqual({ input: "", output: "", enabled: false, device: "" });
    expect(submenu(stores, be).length).toBe(0); // a name left behind would keep claiming a device that is gone
  }, TRS_PICK);
});

test("Forget hands back a REFUSED claim's reservation, which is this process's to release", () => {
  // "in use by another RetroPlug" keeps the pair out of THIS process's shared stream on purpose (its pads must not
  // reach a cart), and the link is not connected behind it. Forget must still release: a pair left reserved stays
  // out of the stream for the rest of the session, and the error stays on the status row.
  withHost((h) => h.away.add("LPProMK3 MIDI"), (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    expect(h.claims).toEqual([]);
    expect(h.status().error.startsWith("MIDI")).toBe(true); // the device is away, the claim refused
    const before = h.releaseCalls;
    forgetLaunchpad(stores.userConfig);
    expect(h.releaseCalls).toBe(before + 1);
    expect(h.status().error).toBe(""); // and the row's error is gone with the reservation
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", device: "Launchpad Pro [MK3]", enabled: true });
});

// --- startup restore, scan ownership, profiles -------------------------------------------------------------

test("the startup restore tells the host it is done, claim or no claim", () => {
  // The standalone holds its MIDI stream and audio back until it hears this, so a restored device is reserved
  // before that stream opens and costs no audio restart. Silence would leave the host waiting out its bound.
  withHost(() => {}, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    expect(h.restoredCalls).toBe(1);
    expect(h.claims).toEqual([]);
  });
  withHost(() => {}, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    expect(h.restoredCalls).toBe(1);
    expect(h.claims.length).toBe(1);
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("while a scan runs, the selection is the scan's: manual edits are greyed and refused", () => {
  // Otherwise a pick made mid-scan is stomped when the scan settles - and on Windows a mid-scan Connect fails outright,
  // since the scan holds every input and a Windows MIDI input is exclusive. The case starts from a SAVED selection,
  // so the Forget row would be ENABLED without the scan guard: it is the scan, not an empty pair, that greys it.
  withHost((h) => h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY }),
    (h, stores, be, clock) => {
      stores.userConfig.setSection("controlSurface", {
        input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", device: "Launchpad Pro [MK3]", enabled: true,
      });
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      const rows = submenu(stores, be);
      for (const id of ["lp-input", "lp-output", "lp-connect"]) expect(findItem(rows, id)?.disabled).toBe(true);
      expect(findItem(midiSettingsRows(stores, be), "midi-scan-forget")?.disabled).toBe(true);
      // ...and refused even if called directly: the saved pair is still there afterwards (a Forget that did nothing
      // must not read as one that cleared it), and nothing was claimed.
      setLaunchpadPorts(stores.userConfig, "Arturia KeyStep 32", "LPProMK3 MIDI");
      connectLaunchpad(stores.userConfig, true, getLaunchpadConfig(stores.userConfig));
      forgetLaunchpad(stores.userConfig);
      expect(stores.userConfig.section("controlSurface")).toEqual({
        input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true, device: "Launchpad Pro [MK3]",
      });
      expect(h.claims).toEqual([]);
      applyLaunchpadScan(stores.userConfig, runScan(clock)!);
      expect(stores.userConfig.section("controlSurface").input).toBe("MIDISPORT 2x2 Port A");
      // Once it is over, the rows are live again.
      expect(findItem(submenu(stores, be), "lp-connect")?.disabled).toBe(undefined);
    });
});

test("other Novation gear answering the inquiry is not taken for a Launchpad", () => {
  // Same manufacturer bytes, a family we have no profile for (a Launchkey, a Circuit). Taking it would claim somebody's
  // keyboard out of the shared MIDI stream and send it a Pro MK3's mode messages.
  const OTHER_NOVATION = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x37, 0x01, 0, 0, 0, 1, 2, 3, 0xf7];
  withHost((h) => {
    h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: OTHER_NOVATION });
    h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY });
  }, (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    // Asked FIRST, answered first - and still passed over for the Launchpad behind it.
    expect(applyLaunchpadScan(stores.userConfig, runScan(clock)!)?.input).toBe("MIDISPORT 2x2 Port A");
  });
  withHost((h) => h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: OTHER_NOVATION }),
    (_h, stores, be, clock) => {
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      expect(applyLaunchpadScan(stores.userConfig, runScan(clock)!)).toBe(null);
      expect(stores.userConfig.section("controlSurface").input).toBe("");
      // Said, rather than "Nothing answered" - something did answer.
      expect(findItem(midiSettingsRows(stores, be), "midi-scan-status")?.label)
        .toBe("Control Surface: Novation device (family 37 01) - not supported");
    });
});

test("a finished scan is applied once, even if the UI remounts and the poll sees it again", () => {
  withHost((h) => h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY }),
    (_h, stores, be, clock) => {
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      const done = runScan(clock)!;
      expect(applyLaunchpadScan(stores.userConfig, done)?.input).toBe("MIDISPORT 2x2 Port A");
      findItem(submenu(stores, be), "lp-input")?.onCycle?.(-1); // the user picks something else afterwards
      const picked = stores.userConfig.section("controlSurface").input;
      expect(applyLaunchpadScan(stores.userConfig, pollLaunchpadScan()!)).toBe(null); // the remounted poll
      expect(stores.userConfig.section("controlSurface").input).toBe(picked);
    });
});

test("a scan opens every input in one batch", () => {
  withHost(() => {}, (h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    runScan(clock);
    expect(h.inputBatches).toBe(1);
  });
});

// --- model confirmation, cancel, labels, retry -------------------------------------------------------------

test("a Mini MK3 answers with the Pro's own family bytes - and is still not taken for one", () => {
  // Both manuals give 13 01. Only the confirm stage tells them apart: the Pro answers a question in its own SysEx
  // header (0E); a Mini ignores it. Taken, a Mini would be claimed and sent messages it ignores.
  withHost((h) => h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: PRO_MK3_REPLY, isPro: false }),
    (h, stores, be, clock) => {
      findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
      const s = runScan(clock)!;
      expect(applyLaunchpadScan(stores.userConfig, s)).toBe(null);
      expect(stores.userConfig.section("controlSurface").input).toBe("");
      // It WAS asked: the candidate got the Pro's model query on the pair it answered on.
      expect(h.written.some((w) => w.port === "LPProMK3 MIDI" && w.bytes.length === 8 && w.bytes[5] === 0x0e)).toBe(true);
      expect(findItem(midiSettingsRows(stores, be), "midi-scan-status")?.label)
        .toBe("Control Surface: Novation device (family 13 01, not a Launchpad Pro [MK3]) - not supported");
    });
});

test("a Mini answering first does not hide the Pro behind it", () => {
  withHost((h) => {
    h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: PRO_MK3_REPLY, isPro: false });
    h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY });
  }, (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    expect(applyLaunchpadScan(stores.userConfig, runScan(clock)!)?.input).toBe("MIDISPORT 2x2 Port A");
  });
});

test("the model is confirmed on the pair that answered, without reopening a single port", () => {
  // The question goes out as a follow-up while the sweep still holds the pair. A second stage would reopen it - another
  // Windows round of stepping the shared stream aside, and a window for another program to grab the port and make a
  // real Pro look silent.
  withHost((h) => h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: PRO_MK3_REPLY }), (h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    const s = runScan(clock)!;
    expect(s.found?.name).toBe(PRO_MK3.name);
    expect(h.inputBatches).toBe(1);
    expect(h.written.filter((w) => w.port === "LPProMK3 MIDI").map((w) => w.bytes))
      .toEqual([deviceInquiry(), modelQuery(PRO_MK3)]);
  });
});

test("Cancel ends a scan, closes its ports, and records nothing it found", () => {
  withHost((h) => h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: PRO_MK3_REPLY }), (h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    clock.ms += SCAN_WINDOW_MS - 10;
    pollLaunchpadScan(); // the Pro has answered the inquiry by now
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.(); // "Cancel Scan"
    expect(h.openHandles()).toBe(0);
    const s = pollLaunchpadScan()!;
    expect(s.busy).toBe(false);
    expect(applyLaunchpadScan(stores.userConfig, s)).toBe(null);
    expect(stores.userConfig.section("controlSurface").input).toBe("");
    expect(findItem(midiSettingsRows(stores, be), "midi-scan-status")?.label).toBe("Control Surface: Cancelled");
  });
});

test("'not supported' still says when ports could not be looked through", () => {
  // On Windows the Launchpad may be behind exactly the port another program holds.
  const OTHER = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x37, 0x01, 0, 0, 0, 1, 2, 3, 0xf7];
  withHost((h) => {
    h.answers.set("LPProMK3 MIDI", { input: "LPProMK3 MIDI", bytes: OTHER });
    h.busyPorts.add("MIDISPORT 2x2 Port A");
  }, (_h, stores, be, clock) => {
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    runScan(clock);
    expect(findItem(midiSettingsRows(stores, be), "midi-scan-status")?.label)
      .toBe("Control Surface: Novation device (family 37 01) - not supported (2 port(s) in use elsewhere)");
  });
});

test("a saved pair whose device was away offers Connect again, and a way to stop retrying", () => {
  withHost((h) => h.away.add("LPProMK3 MIDI"), (h, stores, be) => {
    restoreLaunchpad(stores.userConfig); // the device is away: the claim fails
    let rows = submenu(stores, be);
    expect(findItem(rows, "lp-connect")?.label).toBe("Connect"); // not "Disconnect" - nothing is connected
    h.away.clear(); // plugged back in
    findItem(rows, "lp-connect")?.onSelect?.();
    expect(h.current()?.input).toBe("LPProMK3 MIDI");
    expect(findItem(submenu(stores, be), "lp-stop")).toBe(undefined); // connected: no retry row
    h.held = null; // and away again
    h.away.add("LPProMK3 MIDI");
    rows = submenu(stores, be);
    findItem(rows, "lp-stop")?.onSelect?.();
    expect(stores.userConfig.section("controlSurface").enabled).toBe(false); // no longer reclaimed at launch
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("re-applying the pair already held does not re-claim it", () => {
  // A re-claim releases first, and the release replays the close bytes: the device would drop out of Programmer mode on
  // every remount restore or same-port pick.
  withHost(() => {}, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    restoreLaunchpad(stores.userConfig);
    setLaunchpadPorts(stores.userConfig, "LPProMK3 MIDI", "LPProMK3 MIDI");
    expect(h.claims.length).toBe(1);
    expect(h.released).toEqual([]);
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

// --- releasing the device, names that move, echoes ---------------------------------------------------------

test("switching the controller off in the project lets the device go, with the pair still claimed", () => {
  // The session just drops the role: nothing there can say goodbye, and a Launchpad would stay in Programmer mode with
  // its front panel locked. So the UI sends the claim's close bytes once the role has gone.
  withHost(() => {}, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    pollControllerRole(false); // at startup: nothing was running, nothing to let go of
    expect(h.sentToDevice).toEqual([]);
    stores.project.setSection(CONTROLLER_SECTION, { enabled: true });
    pollControllerRole(true);
    stores.project.setSection(CONTROLLER_SECTION, { enabled: false });
    pollControllerRole(false);
    expect(h.sentToDevice).toEqual([]); // not yet: the role's last blocks may still be on their way out
    for (let i = 1; i < RELEASE_DELAY_POLLS; i++) pollControllerRole(false);
    expect(h.sentToDevice).toEqual([exitToLiveMode(PRO_MK3)]);
    expect(h.held !== null).toBe(true); // still claimed: switching it back on brings a role that retakes it
    pollControllerRole(false);
    expect(h.sentToDevice.length).toBe(1); // once
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("switching it back on before the release cancels the release", () => {
  withHost(() => {}, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    pollControllerRole(true);
    pollControllerRole(false);
    pollControllerRole(true); // a quick off-on: the role is back, so there is nothing to let go
    for (let i = 0; i < RELEASE_DELAY_POLLS + 2; i++) pollControllerRole(true);
    expect(h.sentToDevice).toEqual([]);
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("an interface that echoes SysEx back does not confirm itself as a Pro", () => {
  // A soft-thru answers the model query with the query. The reply must have the manual's select-layout shape.
  withHost((h) => {
    h.answers.set("MIDISPORT 2x2 Port A", { input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY, isPro: false });
  }, (h, stores, be, clock) => {
    setLaunchpadClient(h.sending((_handle, bytes) => {
      if (bytes[0] === 0xf0 && bytes[1] === 0x00) h.hear("MIDISPORT 2x2 Port A", bytes); // the echo
    }));
    findItem(midiSettingsRows(stores, be), "midi-scan")?.onSelect?.();
    expect(applyLaunchpadScan(stores.userConfig, runScan(clock)!)).toBe(null);
  });
});

test("a saved port renamed by the backend is found again by its stem", () => {
  // ALSA names carry the client number and Windows an index; both move when other devices come and go.
  withHost((h) => {
    h.inputs = ["LPProMK3 MIDI:LPProMK3 MIDI 131:0"];
    h.outputs = ["LPProMK3 MIDI:LPProMK3 MIDI 131:0"];
  }, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    expect(h.current()?.input).toBe("LPProMK3 MIDI:LPProMK3 MIDI 131:0");
    expect(stores.userConfig.section("controlSurface").input).toBe("LPProMK3 MIDI:LPProMK3 MIDI 131:0"); // and re-saved
  }, { input: "LPProMK3 MIDI:LPProMK3 MIDI 128:0", output: "LPProMK3 MIDI:LPProMK3 MIDI 128:0", enabled: true });
  // Two candidates with one stem (two identical interfaces): not guessed between.
  withHost((h) => {
    h.inputs = ["IF 1", "IF 2"];
    h.outputs = ["IF 1", "IF 2"];
  }, (h, stores) => {
    restoreLaunchpad(stores.userConfig);
    expect(stores.userConfig.section("controlSurface").input).toBe("IF 0"); // left as saved (the facet refuses it: not here)
    expect(h.claims.some((c) => c.input === "IF 1" || c.input === "IF 2")).toBe(false);
  }, { input: "IF 0", output: "IF 0", enabled: true });
});

test("the port pickers are greyed while connected", () => {
  // Stepping one live would claim every pair it passed, sending Programmer-mode bytes to each device.
  withHost(() => {}, (_h, stores, be) => {
    restoreLaunchpad(stores.userConfig);
    const rows = submenu(stores, be);
    expect(findItem(rows, "lp-input")?.disabled).toBe(true);
    expect(findItem(rows, "lp-output")?.disabled).toBe(true);
    expect(findItem(rows, "lp-connect")?.disabled).toBe(undefined); // Disconnect is how to change them
  }, { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true });
});

test("the USB hint is the device's own MIDI port, not its other interfaces as Windows names them", () => {
  expect(looksLikeLaunchpad("LPProMK3 MIDI")).toBe(true);
  expect(looksLikeLaunchpad("LPProMK3 MIDI 0")).toBe(true); // RtMidi's Windows index
  expect(looksLikeLaunchpad("LPProMK3 MIDI:LPProMK3 MIDI 128:0")).toBe(true); // ALSA, the client named after the port
  // A Pro MK3 on a kernel ALSA client is named after the CARD, and the port after the device: the hint is the port's.
  expect(looksLikeLaunchpad("Launchpad Pro MK3:Launchpad Pro MK3 LPProMK3 MIDI 24:0")).toBe(true);
  expect(looksLikeLaunchpad("Focusrite:Launchpad Pro MK3 LPProMK3 MIDI 32:0")).toBe(true); // and as a suffix
  expect(looksLikeLaunchpad("MIDIIN2 (LPProMK3 MIDI) 1")).toBe(false); // the DIN interface, on Windows
  expect(looksLikeLaunchpad("MIDIOUT3 (LPProMK3 MIDI) 2")).toBe(false); // the DAW interface
  // A prefix or a whole word of the port, not a substring anywhere in it.
  expect(looksLikeLaunchpad("Focusrite:My LPProMK3 MIDI Thing 1")).toBe(false);
});
