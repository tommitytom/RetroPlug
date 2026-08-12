// The Launchpad submenu and the native seam under it (launchpadDevices.ts).
//
// The gating is the interesting part, and it is deliberately UNLIKE the N8 Pro submenu next to it. N8 renders
// only when a cart is actually detected, because a cart is identified by its USB VID:PID and there is no other
// way to attach one. A Launchpad also speaks TRS/DIN: on a machine short of USB ports it arrives through an
// ordinary MIDI interface, on a port named after the INTERFACE, and nothing in that name says "Launchpad". So
// this submenu is gated on the SEAM, every hardware port is offered, and the device hint only picks a default
// and tags a row. A detection gate would hide the only menu that could configure exactly that setup.
//
// The other thing pinned here is the FAREWELL. Programmer mode locks the device's own Settings menu, so native
// must be holding the release bytes before it can ever be handed a device. We stub the __rp_* globals (no MIDI
// system in the pure-TS host) and assert on what the UI hands down.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { PRO_MK3, exitToLiveMode } from "../../src/launchpad";
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
  connected: boolean;
  enabled: boolean;
  sent: number;
  dropped: number;
  error: string;
}

// A machine with a Pro MK3 on USB, an unrelated keyboard, and a MIDI interface - the three cases the port
// cyclers have to handle at once.
const DEFAULT_CFG: CfgStub = {
  inputs: ["Arturia KeyStep 32", "LPProMK3 MIDI", "MIDISPORT 2x2 Port A"],
  outputs: ["LPProMK3 MIDI", "MIDISPORT 2x2 Port A"],
  selectedInput: "",
  selectedOutput: "",
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
}

function installLaunchpad(cfg?: Partial<CfgStub> | null): { calls: Calls; restore: () => void } {
  const g = globalThis as Record<string, unknown>;
  const calls: Calls = { ports: [], connect: [], farewell: [] };
  const saved = {
    get: g.__rp_getLaunchpadConfig,
    ports: g.__rp_setLaunchpadPorts,
    connect: g.__rp_connectLaunchpad,
    farewell: g.__rp_setLaunchpadFarewell,
  };
  if (cfg === null) {
    // No seam at all (a DAW / the headless harness): every hook absent.
    g.__rp_getLaunchpadConfig = undefined;
    g.__rp_setLaunchpadPorts = undefined;
    g.__rp_connectLaunchpad = undefined;
    g.__rp_setLaunchpadFarewell = undefined;
  } else {
    const merged: CfgStub = { ...DEFAULT_CFG, ...cfg };
    g.__rp_getLaunchpadConfig = () => merged;
    g.__rp_setLaunchpadPorts = (i: string, o: string) => calls.ports.push([i, o]);
    g.__rp_connectLaunchpad = (on: boolean) => calls.connect.push(on);
    g.__rp_setLaunchpadFarewell = (b: number[]) => calls.farewell.push([...b]);
  }
  return {
    calls,
    restore: () => {
      g.__rp_getLaunchpadConfig = saved.get;
      g.__rp_setLaunchpadPorts = saved.ports;
      g.__rp_connectLaunchpad = saved.connect;
      g.__rp_setLaunchpadFarewell = saved.farewell;
    },
  };
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

test("the submenu shows with NOTHING plugged in - a TRS surface matches no name", () => {
  const lp = installLaunchpad({ inputs: [], outputs: [] });
  try {
    const be = new MockBackend();
    expect(submenu(composeAppStores({ backend: be }), be).length > 0).toBe(true);
  } finally {
    lp.restore();
  }
});

test("no seam, no submenu", () => {
  const lp = installLaunchpad(null);
  try {
    const be = new MockBackend();
    expect(submenu(composeAppStores({ backend: be }), be).length).toBe(0);
  } finally {
    lp.restore();
  }
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
  // The TRS case: nothing here is named like a Launchpad, so the user has to say which port it is on. Better
  // to connect nothing than to claim somebody's keyboard.
  const lp = installLaunchpad({ inputs: ["Arturia KeyStep 32"], outputs: ["Arturia KeyStep 32"] });
  try {
    const be = new MockBackend();
    const rows = submenu(composeAppStores({ backend: be }), be);
    findItem(rows, "lp-connect")?.onSelect?.();
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
