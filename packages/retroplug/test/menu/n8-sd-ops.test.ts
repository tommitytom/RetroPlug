// The N8 Pro submenu + its SD-card ops (Load ROM / Dump / Restore SRAM). The submenu lives in the instance
// menu's tracker block (beside risa/LSDj) and on the start menu, and it renders ONLY when a physical N8 is
// actually detected (a serial port flagged isN8 by its USB VID:PID) - no hardware, no menu. Within it, the SD
// ops appear only once the streaming link is up (connected), or while a job runs (a job blips the link down to
// borrow the port). The Port cycler offers only detected N8 ports, never plain serial ports. Each op browses a
// local file then calls its native hook, is disabled while a job runs, and the read-only SD row renders the
// polled status snapshot. We stub the __rp_* globals (the native worker isn't present in the pure-TS test host)
// and drive the browse through the MockBackend, exactly like leaves.test does for the file-dialog leaves.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, buildStartMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { nesRom } from "../systems/fixtures";

// The MenuContext App.tsx assembles from its hooks, minus React (mirrors leaves.test's ctxOf).
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

const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);
function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

// Seed a plain NES system once (no tracker role -> the block holds N8 Pro alone), then return the "inst-n8"
// submenu's children. The submenu only exists when an N8 is detected (the default stub has an isN8 port).
function n8Submenu(stores: AppStores, be: MockBackend): MenuItem[] {
  if (stores.project.systems.view().length === 0) {
    be.seed("/roms/a.nes", nesRom());
    stores.project.systems.addSystem("/roms/a.nes");
  }
  const sys = stores.project.systems.view()[0];
  return submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-n8");
}

// A leaf's onSelect fires the browse fire-and-forget; a few microtask turns settle the .then chain.
const flush = async () => {
  for (let i = 0; i < 8; i++) await Promise.resolve();
};

interface Sd {
  busy?: boolean;
  op?: string;
  bytesDone?: number;
  bytesTotal?: number;
  phase?: string;
  done?: boolean;
  error?: string;
  result?: string;
  version?: number;
}
interface Calls {
  load: string[];
  dump: string[];
  restore: string[];
  setPort: string[];
}
interface CfgStub {
  ports: { port: string; isN8: boolean }[];
  selectedPort: string;
  connected: boolean;
  enabled: boolean;
  lookaheadMs: number;
  bytes: number;
  error: string;
}

// The default stub host: one attached N8, link up. Most tests want the ops available, so connected defaults
// true; the gating tests override connected / ports to exercise the hidden states.
const DEFAULT_CFG: CfgStub = {
  ports: [{ port: "/dev/ttyACM0", isN8: true }],
  selectedPort: "/dev/ttyACM0",
  connected: true,
  enabled: true,
  lookaheadMs: 10,
  bytes: 0,
  error: "",
};

// Install the __rp_* N8 hooks the UI reads (getN8Config makes the submenu appear when a port isN8; getN8SdStatus
// + the three op hooks make the SD rows appear + record their calls; setN8Port records port picks). Returns a
// spy record + a restore().
function installN8(sd: Sd | null, cfg?: Partial<CfgStub>): { calls: Calls; restore: () => void } {
  const g = globalThis as Record<string, unknown>;
  const calls: Calls = { load: [], dump: [], restore: [], setPort: [] };
  const saved = {
    cfg: g.__rp_getN8Config,
    sd: g.__rp_getN8SdStatus,
    load: g.__rp_n8LoadRom,
    dump: g.__rp_n8DumpSram,
    restore: g.__rp_n8RestoreSram,
    setPort: g.__rp_setN8Port,
  };
  const merged: CfgStub = { ...DEFAULT_CFG, ...cfg };
  g.__rp_getN8Config = () => merged;
  g.__rp_getN8SdStatus = sd === null ? undefined : () => sd;
  g.__rp_n8LoadRom = (p: string) => calls.load.push(p);
  g.__rp_n8DumpSram = (p: string) => calls.dump.push(p);
  g.__rp_n8RestoreSram = (p: string) => calls.restore.push(p);
  g.__rp_setN8Port = (p: string) => calls.setPort.push(p);
  return {
    calls,
    restore: () => {
      g.__rp_getN8Config = saved.cfg;
      g.__rp_getN8SdStatus = saved.sd;
      g.__rp_n8LoadRom = saved.load;
      g.__rp_n8DumpSram = saved.dump;
      g.__rp_n8RestoreSram = saved.restore;
      g.__rp_setN8Port = saved.setPort;
    },
  };
}

test("N8 Pro (instance menu) shows the SD ops when connected, and each browses a local file then calls its native hook", async () => {
  const env = installN8({ busy: false, op: "", done: false, version: 0 });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores, be);
    expect(findItem(rows, "n8-load-rom")).toBeTruthy();
    expect(findItem(rows, "n8-dump-sram")).toBeTruthy();
    expect(findItem(rows, "n8-restore-sram")).toBeTruthy();

    // Load ROM: a ROM-only browser (patterns include *.nes), then __rp_n8LoadRom with the pick.
    be.queueBrowse("/roms/game.nes");
    findItem(rows, "n8-load-rom")!.onSelect!();
    await flush();
    expect(env.calls.load).toEqual(["/roms/game.nes"]);
    expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].patterns.includes("*.nes")).toBeTruthy();

    // Dump SRAM: a save dialog, then __rp_n8DumpSram with the pick.
    be.queueBrowse("/saves/out.srm");
    findItem(n8Submenu(stores, be), "n8-dump-sram")!.onSelect!();
    await flush();
    expect(env.calls.dump).toEqual(["/saves/out.srm"]);
    expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].saving).toBe(true);

    // Restore SRAM: a read dialog, then __rp_n8RestoreSram with the pick.
    be.queueBrowse("/saves/in.srm");
    findItem(n8Submenu(stores, be), "n8-restore-sram")!.onSelect!();
    await flush();
    expect(env.calls.restore).toEqual(["/saves/in.srm"]);
    expect(!!be.fileBrowserCalls[be.fileBrowserCalls.length - 1].saving).toBe(false);
  } finally {
    env.restore();
  }
});

test("the SD ops are hidden until the N8 link is connected (streaming config still shows)", () => {
  const env = installN8({ busy: false, op: "", done: false, version: 0 }, { connected: false, enabled: false });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores, be);
    // Submenu present (an N8 is detected) with the streaming config, but no SD ops until you Connect.
    expect(findItem(rows, "n8-port")).toBeTruthy();
    expect(findItem(rows, "n8-connect")).toBeTruthy();
    expect(findItem(rows, "n8-status")).toBeTruthy();
    expect(findItem(rows, "n8-load-rom")).toBe(undefined);
    expect(findItem(rows, "n8-dump-sram")).toBe(undefined);
    expect(findItem(rows, "n8-restore-sram")).toBe(undefined);
    expect(findItem(rows, "n8-sd-status")).toBe(undefined);
    expect(findItem(rows, "n8-sep-sd")).toBe(undefined);
  } finally {
    env.restore();
  }
});

test("the SD ops stay visible (and disabled) while a job runs, even though the link is torn down mid-job", () => {
  // A running job disconnects the link to borrow the port, so connected blips false - `busy` must keep the
  // block + its live progress row put.
  const env = installN8(
    { busy: true, op: "load", phase: "Uploading", bytesDone: 45, bytesTotal: 100, done: false, version: 3 },
    { connected: false },
  );
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores, be);
    expect(findItem(rows, "n8-load-rom")!.disabled).toBe(true);
    expect(findItem(rows, "n8-dump-sram")!.disabled).toBe(true);
    expect(findItem(rows, "n8-restore-sram")!.disabled).toBe(true);
    expect(findItem(rows, "n8-connect")!.disabled).toBe(true); // streaming Connect is disabled while an SD op runs
    expect(findItem(rows, "n8-sd-status")!.label).toBe("SD: Uploading 45%");
  } finally {
    env.restore();
  }
});

test("the SD status row shows the last result when idle (and connected)", () => {
  const env = installN8({ busy: false, op: "dump", done: true, result: "Dumped 64 KB to out.srm", version: 5 });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    expect(findItem(n8Submenu(stores, be), "n8-sd-status")!.label).toBe("SD: Dumped 64 KB to out.srm");
  } finally {
    env.restore();
  }
});

test("N8 Pro also appears on the start menu (no system loaded), with the same ops", () => {
  const env = installN8({ busy: false, op: "", done: false, version: 0 });
  try {
    const stores = composeAppStores({ backend: new MockBackend("/cfg") });
    const rows = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-n8");
    expect(findItem(rows, "n8-port")).toBeTruthy(); // streaming config
    expect(findItem(rows, "n8-load-rom")).toBeTruthy(); // SD ops (no system needed)
    expect(findItem(rows, "n8-dump-sram")).toBeTruthy();
  } finally {
    env.restore();
  }
});

test("the whole N8 Pro submenu is hidden when no N8 is detected (no isN8 port)", () => {
  const env = installN8({ busy: false, version: 0 }, { ports: [{ port: "/dev/ttyUSB0", isN8: false }], selectedPort: "" });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    // Start menu: no N8 Pro item (and no fencing separator for it).
    const start = buildStartMenu(ctxOf(stores)).items;
    expect(findItem(start, "start-n8")).toBe(undefined);
    expect(findItem(start, "start-sep-n8")).toBe(undefined);
    // Instance menu: seed a plain NES (no tracker) -> the whole tracker/N8 block collapses, no inst-n8.
    be.seed("/roms/a.nes", nesRom());
    stores.project.systems.addSystem("/roms/a.nes");
    const sys = stores.project.systems.view()[0];
    const inst = buildInstanceMenu({ ...ctxOf(stores), system: sys }).items;
    expect(findItem(inst, "inst-n8")).toBe(undefined);
    expect(findItem(inst, "inst-sep-tracker")).toBe(undefined);
  } finally {
    env.restore();
  }
});

test("the Port cycler offers only detected N8 ports, never plain serial ports", () => {
  // Two ports: one real N8, one plain serial. The cycler must expose only "(auto-detect)" + the N8, so cycling
  // backward from auto-detect (index 0) wraps to the N8 - never the plain port.
  const env = installN8(
    { busy: false, version: 0 },
    { ports: [{ port: "/dev/ttyACM0", isN8: true }, { port: "/dev/ttyS0", isN8: false }], selectedPort: "" },
  );
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const port = findItem(n8Submenu(stores, be), "n8-port")!;
    expect(port.label).toBe("Port: (auto-detect)"); // no "[N8]" tag; the plain port isn't shown
    port.onCycle!(-1); // wrap backward to the last entry -> the only real port is the N8
    expect(env.calls.setPort).toEqual(["/dev/ttyACM0"]);
  } finally {
    env.restore();
  }
});

test("a persisted port that isn't currently detected is still shown, tagged '(not detected)'", () => {
  const env = installN8(
    { busy: false, version: 0 },
    { ports: [{ port: "/dev/ttyACM0", isN8: true }], selectedPort: "/dev/ttyOLD" },
  );
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    expect(findItem(n8Submenu(stores, be), "n8-port")!.label).toBe("Port: /dev/ttyOLD (not detected)");
  } finally {
    env.restore();
  }
});

test("the SD rows are absent when the host lacks the SD seam (streaming-only submenu still builds)", () => {
  const env = installN8(null); // __rp_getN8SdStatus undefined -> the SD block can't appear; streaming config still present
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores, be);
    expect(findItem(rows, "n8-load-rom")).toBe(undefined);
    expect(findItem(rows, "n8-sd-status")).toBe(undefined);
    expect(findItem(rows, "n8-status")).toBeTruthy(); // the streaming status row still there
  } finally {
    env.restore();
  }
});
