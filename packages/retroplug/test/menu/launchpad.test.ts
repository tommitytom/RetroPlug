// The Launchpad submenu and the native seam under it (launchpadDevices.ts).
//
// The gating is the interesting part. Like the N8 Pro submenu beside it, this one appears only when the
// hardware is actually there - but a Launchpad cannot be detected the way an N8 is. An N8 is a USB cart with
// a VID:PID and no other way to attach it; a Launchpad also speaks TRS/DIN, arriving through an ordinary MIDI
// interface on a port named after the INTERFACE, and nothing in that name says "Launchpad". Nor is there
// anything to listen for: DIN has no enumeration and no idle heartbeat. So presence means one of three
// things, all pinned below - a port named like the device's own USB interface, a live link, or a recorded
// pick whose BOTH ports are still enumerated. That last one is what a Settings > MIDI scan writes, and the
// reason the scan lives in Settings: it is reachable when this submenu is not.
//
// The scan itself is a Universal Device Inquiry. Native writes opaque bytes and reports opaque answers; the
// bytes and their meaning are TS's, so the cases here assert exactly that - the probe we hand down IS
// deviceInquiry(), and only a Novation answer is acted on.
//
// The other thing pinned here is the FAREWELL. Programmer mode locks the device's own Settings menu, so native
// must be holding the release bytes before it can ever be handed a device. We stub the __rp_* globals (no MIDI
// system in the pure-TS host) and assert on what the UI hands down.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { PRO_MK3, deviceInquiry, exitToLiveMode } from "../../src/launchpad";
import {
  applyLaunchpadScan, connectLaunchpad, getLaunchpadConfig, getLaunchpadScan,
} from "../../ui/screens/menu/launchpadDevices";
import { nesRom } from "../systems/fixtures";

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
    openLsdjHd: () => {},
  };
}

function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}
const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);

interface CfgStub {
  inputs: string[];
  outputs: string[];
  selectedInput: string;
  selectedOutput: string;
  deviceLabel: string;
  connected: boolean;
  enabled: boolean;
  sent: number;
  dropped: number;
  error: string;
}

interface ScanStub {
  busy: boolean;
  done: boolean;
  phase: string;
  error: string;
  skipped: number;
  version: number;
  replies: { output: string; input: string; bytes: number[] }[];
}

const IDLE_SCAN: ScanStub = { busy: false, done: false, phase: "", error: "", skipped: 0, version: 0, replies: [] };

/** A Pro MK3's Device Inquiry answer: F0 7E <dev> 06 02 <Novation> <family 13 01> 00 00 <version> F7. */
const PRO_MK3_REPLY = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0, 0, 0, 1, 2, 3, 0xf7];

// A machine with a Pro MK3 on USB, an unrelated keyboard, and a MIDI interface - the three cases the port
// cyclers have to handle at once.
const DEFAULT_CFG: CfgStub = {
  inputs: ["Arturia KeyStep 32", "LPProMK3 MIDI", "MIDISPORT 2x2 Port A"],
  outputs: ["LPProMK3 MIDI", "MIDISPORT 2x2 Port A"],
  selectedInput: "",
  selectedOutput: "",
  deviceLabel: "",
  connected: false,
  enabled: false,
  sent: 0,
  dropped: 0,
  error: "",
};

interface Calls {
  ports: [string, string][];
  connect: boolean[];
  farewell: number[][];
  scans: { probe: number[]; windowMs: number }[];
  device: string[];
}

function installLaunchpad(cfg?: Partial<CfgStub> | null, scan?: Partial<ScanStub>): { calls: Calls; restore: () => void } {
  const g = globalThis as Record<string, unknown>;
  const calls: Calls = { ports: [], connect: [], farewell: [], scans: [], device: [] };
  const keys = [
    "__rp_getLaunchpadConfig", "__rp_setLaunchpadPorts", "__rp_connectLaunchpad", "__rp_setLaunchpadFarewell",
    "__rp_scanLaunchpad", "__rp_getLaunchpadScan", "__rp_setLaunchpadDevice",
  ];
  const saved = Object.fromEntries(keys.map((k) => [k, g[k]]));
  if (cfg === null) {
    // No seam at all (a DAW / the headless harness): every hook absent.
    for (const k of keys) g[k] = undefined;
  } else {
    const merged: CfgStub = { ...DEFAULT_CFG, ...cfg };
    const scanState: ScanStub = { ...IDLE_SCAN, ...scan };
    g.__rp_getLaunchpadConfig = () => merged;
    g.__rp_setLaunchpadPorts = (i: string, o: string) => calls.ports.push([i, o]);
    g.__rp_connectLaunchpad = (on: boolean) => calls.connect.push(on);
    g.__rp_setLaunchpadFarewell = (b: number[]) => calls.farewell.push([...b]);
    g.__rp_scanLaunchpad = (probe: number[], windowMs: number) => {
      calls.scans.push({ probe: [...probe], windowMs });
      return true;
    };
    g.__rp_getLaunchpadScan = () => scanState;
    g.__rp_setLaunchpadDevice = (label: string) => calls.device.push(label);
  }
  return {
    calls,
    restore: () => {
      for (const k of keys) g[k] = saved[k];
    },
  };
}

/** Settings > MIDI's rows, which is where a control surface is FOUND. Needs the standalone flags the real
 *  host sets, since the whole MIDI submenu is standalone-only. */
function midiSettingsRows(stores: AppStores, be: MockBackend): MenuItem[] {
  const g = globalThis as Record<string, unknown>;
  const saved = { standalone: g.__rp_isStandalone, midi: g.__rp_getMidiConfig };
  g.__rp_isStandalone = true;
  g.__rp_getMidiConfig = () => ({ inputs: [], outputs: [], selectedInput: "", selectedOutput: "" });
  try {
    if (stores.project.systems.view().length === 0) {
      be.seed("/roms/a.nes", nesRom());
      stores.project.systems.addSystem("/roms/a.nes");
    }
    const sys = stores.project.systems.view()[0];
    const items = buildInstanceMenu({ ...ctxOf(stores), system: sys }).items;
    return submenuChildren(submenuChildren(items, "inst-settings"), "set-midi");
  } finally {
    g.__rp_isStandalone = saved.standalone;
    g.__rp_getMidiConfig = saved.midi;
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

/** Whether the instance menu is showing the Launchpad submenu at all, under this config. */
function submenuShown(cfg: Partial<CfgStub> | null): boolean {
  const lp = installLaunchpad(cfg);
  try {
    const be = new MockBackend();
    return submenu(composeAppStores({ backend: be }), be).length > 0;
  } finally {
    lp.restore();
  }
}

test("no seam, no submenu", () => {
  expect(submenuShown(null)).toBe(false);
});

test("nothing plugged in, no submenu", () => {
  // The whole point of the change: a machine with no control surface does not carry a row for one.
  expect(submenuShown({ inputs: [], outputs: [] })).toBe(false);
  // Neither does a machine with plenty of MIDI gear that is simply not a Launchpad.
  expect(submenuShown({ inputs: ["Arturia KeyStep 32"], outputs: ["Arturia KeyStep 32"] })).toBe(false);
});

test("a Launchpad on USB needs no scan - its own port name is enough", () => {
  expect(submenuShown({ inputs: ["LPProMK3 MIDI"], outputs: ["LPProMK3 MIDI"] })).toBe(true);
});

test("a recorded pick speaks for a device no name could identify", () => {
  // The TRS topology, after a scan: the ports belong to somebody's interface and say nothing about what is
  // plugged into them, so the recorded pair is the only evidence there is.
  const trs = {
    inputs: ["Arturia KeyStep 32", "MIDISPORT 2x2 Port A"],
    outputs: ["MIDISPORT 2x2 Port B"],
    selectedInput: "MIDISPORT 2x2 Port A",
    selectedOutput: "MIDISPORT 2x2 Port B",
    deviceLabel: "Launchpad Pro [MK3]",
  };
  expect(submenuShown(trs)).toBe(true);

  // Presence is checked, not trusted: unplug that interface and the row goes with it, exactly as the N8 Pro
  // submenu next door behaves. Settings > MIDI still has the scan, so there is a way back.
  expect(submenuShown({ ...trs, inputs: ["Arturia KeyStep 32"], outputs: [] })).toBe(false);
  // Half a pair is not a device either - an output alone cannot be listened to.
  expect(submenuShown({ ...trs, outputs: [] })).toBe(false);
});

test("a connected link always shows, whatever the port names look like", () => {
  expect(submenuShown({
    inputs: ["MIDISPORT 2x2 Port A"], outputs: ["MIDISPORT 2x2 Port B"],
    selectedInput: "MIDISPORT 2x2 Port A", selectedOutput: "MIDISPORT 2x2 Port B",
    connected: true, enabled: true,
  })).toBe(true);
});

test("the port cycler offers EVERY hardware port, not just ones named like a Launchpad", () => {
  const lp = installLaunchpad();
  try {
    const be = new MockBackend();
    const rows = submenu(composeAppStores({ backend: be }), be);
    const input = findItem(rows, "lp-input");
    expect(input?.kind).toBe("cycler");
    expect(input?.label).toBe("Input Device: (none)");
    // Stepping forward from "(none)" reaches the FIRST port in the list, which is nothing like a Launchpad.
    // A filtered list would have skipped it - and would have skipped the interface port a TRS-attached
    // Launchpad actually arrives on.
    input?.onCycle?.(1);
    expect(lp.calls.ports[0]?.[0]).toBe("Arturia KeyStep 32");
  } finally {
    lp.restore();
  }
});

test("a hinted port is tagged; an unhinted one is perfectly selectable", () => {
  const hinted = installLaunchpad({ selectedInput: "LPProMK3 MIDI" });
  try {
    const be = new MockBackend();
    expect(findItem(submenu(composeAppStores({ backend: be }), be), "lp-input")?.label)
      .toBe("Input Device: LPProMK3 MIDI (detected)");
  } finally {
    hinted.restore();
  }
  // The TRS topology: the surface is on an interface port, so no tag - and that is not an error state.
  const trs = installLaunchpad({ selectedInput: "MIDISPORT 2x2 Port A" });
  try {
    const be = new MockBackend();
    expect(findItem(submenu(composeAppStores({ backend: be }), be), "lp-input")?.label)
      .toBe("Input Device: MIDISPORT 2x2 Port A");
  } finally {
    trs.restore();
  }
});

test("picking a port sends BOTH names down, with the display tag stripped back off", () => {
  // Cycling forward from the KeyStep lands on the Launchpad, whose ROW READS "LPProMK3 MIDI (detected)". The
  // name handed to native has to be the port's, not the label's, or it would name a port that does not exist.
  const lp = installLaunchpad({ selectedInput: "Arturia KeyStep 32", selectedOutput: "LPProMK3 MIDI" });
  try {
    const be = new MockBackend();
    const rows = submenu(composeAppStores({ backend: be }), be);
    findItem(rows, "lp-input")?.onCycle?.(1);
    expect(lp.calls.ports).toEqual([["LPProMK3 MIDI", "LPProMK3 MIDI"]]);
  } finally {
    lp.restore();
  }
});

test("Connect with nothing chosen resolves the hinted default first", () => {
  const lp = installLaunchpad();
  try {
    const be = new MockBackend();
    const rows = submenu(composeAppStores({ backend: be }), be);
    findItem(rows, "lp-connect")?.onSelect?.();
    expect(lp.calls.ports).toEqual([["LPProMK3 MIDI", "LPProMK3 MIDI"]]);
    expect(lp.calls.connect).toEqual([true]);
  } finally {
    lp.restore();
  }
});

test("Connect with no hinted port at all does not invent one", () => {
  // Driven directly rather than through the row, because the row cannot reach this state any more: with no
  // hint and nothing recorded there is no submenu to press Connect in. The rule still has to hold for the
  // path that does exist - cycling a recorded Input Device back to "(none)" and reconnecting - and the cost
  // of getting it wrong is claiming somebody's keyboard and calling it a Launchpad.
  const lp = installLaunchpad({ inputs: ["Arturia KeyStep 32"], outputs: ["Arturia KeyStep 32"] });
  try {
    connectLaunchpad(true, getLaunchpadConfig());
    expect(lp.calls.ports).toEqual([]);
    expect(lp.calls.connect).toEqual([true]);
  } finally {
    lp.restore();
  }
});

test("native is holding the farewell before anything can connect", () => {
  const lp = installLaunchpad();
  try {
    const be = new MockBackend();
    submenu(composeAppStores({ backend: be }), be); // merely RENDERING the menu is enough
    expect(lp.calls.farewell.length > 0).toBe(true);
    expect(lp.calls.farewell[0]).toEqual(exitToLiveMode(PRO_MK3));
  } finally {
    lp.restore();
  }
});

test("the status row reads what the link is actually doing", () => {
  const cases: [Partial<CfgStub>, string][] = [
    [{}, "Status: not connected"],
    [{ connected: true, enabled: true }, "Status: connected"],
    [{ connected: true, enabled: true, dropped: 3 }, "Status: connected (3 dropped)"],
    [{ enabled: true }, "Status: pick both ports"],
    [{ enabled: true, selectedInput: "a", selectedOutput: "b" }, "Status: not connected"],
    [{ error: "MIDI input port not found: Nope" }, "Status: Error: MIDI input port not found: Nope"],
  ];
  for (const [cfg, label] of cases) {
    const lp = installLaunchpad(cfg);
    try {
      const be = new MockBackend();
      expect(findItem(submenu(composeAppStores({ backend: be }), be), "lp-status")?.label).toBe(label);
    } finally {
      lp.restore();
    }
  }
});

test("the app rows edit PROJECT settings, and each edit re-pushes the kernel", () => {
  const lp = installLaunchpad();
  try {
    const be = new MockBackend();
    const stores = composeAppStores({ backend: be });
    // A cycler's onCycle takes a DIRECTION, so each of these steps once from its default: off -> on,
    // bar -> beat, follow on -> off, and the emulated cart -> MIDI out.
    const rows = submenu(stores, be);
    let pushes = 0; // counted from HERE: seeding the cart above pushes once on its own
    stores.project.setOnSystemsChange(() => void pushes++);

    findItem(rows, "lp-enabled")?.onCycle?.(1);
    findItem(rows, "lp-quantise")?.onCycle?.(-1);
    findItem(rows, "lp-follow")?.onCycle?.(-1);
    findItem(rows, "lp-target")?.onCycle?.(1);

    const c = stores.project.settings().controller;
    expect(c.enabled).toBe(true);
    expect(c.target).toBe("midiOut");
    expect(c.appConfig.quantise).toBe("beat");
    expect(c.appConfig.follow).toBe(false);
    // The controller role is SYNTHESIZED at projection time, so an edit that never reached setSystems would
    // leave the audio thread running the previous settings.
    expect(pushes).toBe(4);
  } finally {
    lp.restore();
  }
});

test("one app knob at a time: setting quantise does not wipe follow", () => {
  const lp = installLaunchpad();
  try {
    const be = new MockBackend();
    const stores = composeAppStores({ backend: be });
    submenu(stores, be);
    stores.project.setController({ appConfig: { follow: false } });
    stores.project.setController({ appConfig: { quantise: "beat" } });
    const c = stores.project.settings().controller;
    expect(c.appConfig.follow).toBe(false);
    expect(c.appConfig.quantise).toBe("beat");
  } finally {
    lp.restore();
  }
});

// --- Settings > MIDI: finding one ---------------------------------------------------------------------

test("the scan row hands native exactly the Device Inquiry, and nothing else does", () => {
  const lp = installLaunchpad();
  try {
    const be = new MockBackend();
    const rows = midiSettingsRows(composeAppStores({ backend: be }), be);
    expect(lp.calls.scans).toEqual([]); // merely opening Settings writes no MIDI at anybody's gear
    findItem(rows, "midi-scan")?.onSelect?.();
    expect(lp.calls.scans.length).toBe(1);
    // The bytes are TS's, not native's: native carries this blob the way it carries the farewell.
    expect(lp.calls.scans[0].probe).toEqual(deviceInquiry());
    expect(lp.calls.scans[0].windowMs > 0).toBe(true);
  } finally {
    lp.restore();
  }
});

test("the scan row steps aside while the link holds the ports", () => {
  const busy = installLaunchpad({}, { busy: true });
  try {
    const be = new MockBackend();
    expect(findItem(midiSettingsRows(composeAppStores({ backend: be }), be), "midi-scan")?.disabled).toBe(true);
  } finally {
    busy.restore();
  }
  // Connected: those ports cannot be reopened, and there is nothing left to find.
  const live = installLaunchpad({ connected: true, enabled: true, selectedInput: "LPProMK3 MIDI" });
  try {
    const be = new MockBackend();
    const rows = midiSettingsRows(composeAppStores({ backend: be }), be);
    expect(findItem(rows, "midi-scan")?.disabled).toBe(true);
    expect(findItem(rows, "midi-scan-forget")?.disabled).toBe(true); // nor is this the moment to forget it
  } finally {
    live.restore();
  }
});

test("a Novation answer records the PAIR it came back on, and names the model", () => {
  // The output probed and the input that answered are different ports, which is the whole reason to ask:
  // no amount of listening tells you which input belongs to which output.
  const lp = installLaunchpad({}, {
    done: true,
    replies: [{ output: "MIDISPORT 2x2 Port B", input: "MIDISPORT 2x2 Port A", bytes: PRO_MK3_REPLY }],
  });
  try {
    const found = applyLaunchpadScan(getLaunchpadScan()!);
    expect(found?.input).toBe("MIDISPORT 2x2 Port A");
    expect(found?.output).toBe("MIDISPORT 2x2 Port B");
    expect(found?.name).toBe(PRO_MK3.name);
    expect(lp.calls.ports).toEqual([["MIDISPORT 2x2 Port A", "MIDISPORT 2x2 Port B"]]);
    expect(lp.calls.device).toEqual([PRO_MK3.name]);
  } finally {
    lp.restore();
  }
});

test("anything that is not a Novation answer is left exactly where it was", () => {
  const cases: [string, number[]][] = [
    // Another manufacturer answering the same universal question. It is a valid inquiry reply, and not ours.
    ["another manufacturer", [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x41, 0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0, 0, 0, 0xf7]],
    // A keyboard on the same port playing a note into the window.
    ["a stray NoteOn", [0x90, 60, 100]],
    ["a truncated reply", [0xf0, 0x7e]],
  ];
  for (const [what, bytes] of cases) {
    const lp = installLaunchpad({}, { done: true, replies: [{ output: "out", input: "in", bytes }] });
    try {
      expect(applyLaunchpadScan(getLaunchpadScan()!)).toBe(null);
      expect(lp.calls.ports).toEqual([]); // `what` must not have claimed somebody's keyboard
      expect(lp.calls.device).toEqual([]);
    } finally {
      lp.restore();
    }
    expect(what.length > 0).toBe(true);
  }
});

test("a Novation answer is found among the noise, not blocked by it", () => {
  const lp = installLaunchpad({}, {
    done: true,
    replies: [
      { output: "out", input: "Keystep", bytes: [0x90, 60, 100] }, // somebody is playing while we scan
      { output: "IF Port B", input: "IF Port A", bytes: PRO_MK3_REPLY },
    ],
  });
  try {
    expect(applyLaunchpadScan(getLaunchpadScan()!)?.input).toBe("IF Port A");
    expect(lp.calls.ports).toEqual([["IF Port A", "IF Port B"]]);
  } finally {
    lp.restore();
  }
});

test("the scan status row says which of 'nothing there' and 'could not look' happened", () => {
  const cases: [Partial<CfgStub>, Partial<ScanStub>, string][] = [
    [{}, {}, "Control Surface: Not scanned"],
    [{}, { busy: true, phase: "Probing MIDISPORT 2x2 Port B" }, "Control Surface: Probing MIDISPORT 2x2 Port B"],
    [{}, { done: true }, "Control Surface: Nothing answered"],
    // A MIDI input is exclusive on Windows, so a port another app holds cannot be listened on at all. Saying
    // so is the difference between "you have no Launchpad" and "close the other program and try again".
    [{}, { done: true, skipped: 2 }, "Control Surface: Nothing answered (2 port(s) in use elsewhere)"],
    // A device recorded by an earlier session, with no scan run since.
    [{ selectedInput: "MIDISPORT 2x2 Port A", deviceLabel: "Launchpad Pro [MK3]" }, {},
     "Control Surface: Launchpad Pro [MK3] on MIDISPORT 2x2 Port A"],
    // A scan that just answered reports itself, without waiting for the result to be applied.
    [{}, { done: true, replies: [{ output: "IF Port B", input: "IF Port A", bytes: PRO_MK3_REPLY }] },
     "Control Surface: Launchpad Pro [MK3] on IF Port A"],
    // ...and outranks a stale recording, so re-scanning after unplugging something says so.
    [{ selectedInput: "MIDISPORT 2x2 Port A", deviceLabel: "Launchpad Pro [MK3]" }, { done: true },
     "Control Surface: Nothing answered"],
    [{}, { error: "MIDI system unavailable" }, "Control Surface: Error: MIDI system unavailable"],
  ];
  for (const [cfg, scan, label] of cases) {
    const lp = installLaunchpad(cfg, scan);
    try {
      const be = new MockBackend();
      expect(findItem(midiSettingsRows(composeAppStores({ backend: be }), be), "midi-scan-status")?.label).toBe(label);
    } finally {
      lp.restore();
    }
  }
});

test("Forget clears the pair AND the name, so the submenu goes away again", () => {
  const lp = installLaunchpad({
    inputs: ["MIDISPORT 2x2 Port A"], outputs: ["MIDISPORT 2x2 Port B"],
    selectedInput: "MIDISPORT 2x2 Port A", selectedOutput: "MIDISPORT 2x2 Port B",
    deviceLabel: "Launchpad Pro [MK3]",
  });
  try {
    const be = new MockBackend();
    findItem(midiSettingsRows(composeAppStores({ backend: be }), be), "midi-scan-forget")?.onSelect?.();
    expect(lp.calls.ports).toEqual([["", ""]]);
    expect(lp.calls.device).toEqual([""]); // a name left behind would keep claiming a device that is gone
  } finally {
    lp.restore();
  }
});

test("the status row names the device a scan read off the hardware", () => {
  const lp = installLaunchpad({ connected: true, enabled: true, deviceLabel: "Launchpad Pro [MK3]" });
  try {
    const be = new MockBackend();
    // "connected" alone reads the same whether the cable reaches a Launchpad or nothing at all.
    expect(findItem(submenu(composeAppStores({ backend: be }), be), "lp-status")?.label)
      .toBe("Status: connected - Launchpad Pro [MK3]");
  } finally {
    lp.restore();
  }
});
