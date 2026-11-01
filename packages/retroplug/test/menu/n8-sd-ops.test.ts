// The N8 Pro submenu + its card operations (Load ROM, back up and restore the battery). The submenu lives in the
// instance menu's tracker block (beside risa/LSDj) and on the start menu, and it renders ONLY when a physical N8 is
// actually detected (a serial port flagged isN8 by its USB VID:PID) - no hardware, no menu. Within it, the card rows
// are capability jobs against the driven cart's rp.n8.nes node (docs/modular-graph-phase-c5-design.md sections 4 to 6):
// they appear while the link is up and the project drives a cart, each browses a local file and then submits its job
// through the MockBackend's model, every row is disabled while one runs, and the read-only row renders the job's
// snapshot (progress, then its result or error). The streaming config (port, Connect, lookahead) is native host state,
// so its hooks stay stubbed here; the jobs are not.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, buildStartMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { getN8CartJob, n8RestoreSram, projectCart, resetN8CartJob } from "../../ui/screens/menu/n8SdOps";
import { nesRom, gbRom } from "../systems/fixtures";

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
    openLsdjHd: () => {},
  };
}

const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);
function submenuChildren(items: MenuItem[], id: string): MenuItem[] {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
}

// Seed an NES system once (no tracker role -> the block holds N8 Pro alone), then return the "inst-n8" submenu's
// children. The submenu only exists when an N8 is detected (the default stub has an isN8 port); the card rows also
// need a system that drives a cart, which a NES does by default.
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

interface CfgStub {
  ports: { port: string; isN8: boolean }[];
  selectedPort: string;
  connected: boolean;
  enabled: boolean;
  lookaheadMs: number;
  bytes: number;
  error: string;
}

// The default stub host: one attached N8, link up. Most tests want the operations available, so connected defaults
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

// Install the __rp_* N8 streaming hooks the UI reads (getN8Config makes the submenu appear when a port isN8; setN8Port
// records port picks). Returns a spy record + a restore().
function installN8(cfg?: Partial<CfgStub>): { calls: { setPort: string[] }; cfg: CfgStub; restore: () => void } {
  resetN8CartJob(); // one case's job does not colour the next one's rows
  const g = globalThis as Record<string, unknown>;
  const calls = { setPort: [] as string[] };
  const saved = { cfg: g.__rp_getN8Config, setPort: g.__rp_setN8Port };
  const merged: CfgStub = { ...DEFAULT_CFG, ...cfg };
  g.__rp_getN8Config = () => merged;
  g.__rp_setN8Port = (p: string) => calls.setPort.push(p);
  return {
    calls,
    cfg: merged,
    restore: () => {
      g.__rp_getN8Config = saved.cfg;
      g.__rp_setN8Port = saved.setPort;
    },
  };
}

test("N8 Pro (instance menu) shows the card rows when connected, and each browses a local file then runs its job",
  async () => {
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores, be);
    expect(findItem(rows, "n8-load-rom")).toBeTruthy();
    expect(findItem(rows, "n8-dump-sram")).toBeTruthy();
    expect(findItem(rows, "n8-restore-sram")).toBeTruthy();
    const cart = projectCart(stores.project.systems.view())!;
    expect(cart.nodeId).toBe(`cart-${cart.system.id}`);

    // Load ROM: a ROM-only browser (patterns include *.nes), then a rom.upload of the file's bytes under its own name.
    be.seed("/roms/game.nes", nesRom());
    be.queueBrowse("/roms/game.nes");
    findItem(rows, "n8-load-rom")!.onSelect!();
    await flush();
    expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].patterns.includes("*.nes")).toBeTruthy();
    const upload = be.jobRequests[be.jobRequests.length - 1];
    expect([upload.nodeId, upload.capability, upload.op, upload.name]).toEqual([
      cart.nodeId, "rp.system.rom/1", "upload", "game.nes",
    ]);
    expect(upload.bytes).toEqual(be.readFile("/roms/game.nes"));
    expect(be.cartRom?.name).toBe("game.nes");
    expect(getN8CartJob().busy).toBe(false);
    expect(getN8CartJob().result).toBe("Booted the ROM on the cart");

    // Dump the battery: a save dialog, then sram.dump, written to the picked path. The job names the ROM this session
    // booted, which is the folder the cart's save file lives in.
    be.queueBrowse("/saves/out.srm");
    findItem(n8Submenu(stores, be), "n8-dump-sram")!.onSelect!();
    await flush();
    expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].saving).toBe(true);
    const dump = be.jobRequests[be.jobRequests.length - 1];
    expect([dump.nodeId, dump.capability, dump.op, dump.name]).toEqual([
      cart.nodeId, "rp.system.sram/1", "dump", "game.nes",
    ]);
    expect(be.readFile("/saves/out.srm")).toEqual(be.cartBattery);
    expect(getN8CartJob().result).toBe("Backed up the cart's battery");

    // Restore: a read dialog, then sram.restore of that file's bytes, which reach the cart's battery file.
    be.seed("/saves/in.srm", new Uint8Array(Array.from({ length: 256 }, (_, i) => i)));
    be.queueBrowse("/saves/in.srm");
    findItem(n8Submenu(stores, be), "n8-restore-sram")!.onSelect!();
    await flush();
    expect(!!be.fileBrowserCalls[be.fileBrowserCalls.length - 1].saving).toBe(false);
    const restore = be.jobRequests[be.jobRequests.length - 1];
    expect([restore.nodeId, restore.capability, restore.op, restore.name]).toEqual([
      cart.nodeId, "rp.system.sram/1", "restore", "game.nes",
    ]);
    expect(be.cartBattery).toEqual(be.readFile("/saves/in.srm"));
    // The row says which file it wrote: the card's own path for that ROM, and that no folder had to be made for it.
    expect(getN8CartJob().result).toBe(
      "Restored the battery to EDN8/gamedata/game.nes/bram.srm (it loads at the next boot)",
    );
  } finally {
    env.restore();
  }
});

test("a restore into a folder the card had to make warns (the ROM's file name on the card may be wrong)", async () => {
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    n8Submenu(stores, be); // a NES at /roms/a.nes that this session booted nothing on the card for
    be.seed("/saves/in.srm", new Uint8Array([1, 2, 3]));
    be.queueBrowse("/saves/in.srm");
    findItem(n8Submenu(stores, be), "n8-restore-sram")!.onSelect!();
    await flush();
    const job = getN8CartJob();
    expect(job.error).toBe("");
    expect(job.result.startsWith("Warning:")).toBe(true);
    expect(job.result.includes("EDN8/gamedata/a.nes/bram.srm")).toBe(true);
    // The bytes still landed: it is where they landed that the row is warning about.
    expect(be.cartBattery).toEqual(be.readFile("/saves/in.srm"));

    // The card's folder exists now, so a second restore to the same name finds it: native lists the folder, and the
    // mock keeps what it made, so neither warns again.
    be.queueBrowse("/saves/in.srm");
    findItem(n8Submenu(stores, be), "n8-restore-sram")!.onSelect!();
    await flush();
    expect(getN8CartJob().result).toBe(
      "Restored the battery to EDN8/gamedata/a.nes/bram.srm (it loads at the next boot)",
    );
  } finally {
    env.restore();
  }
});

test("a file that cannot be read leaves a running job's row alone", async () => {
  // The rows are disabled while a job runs, so nothing in the menu reaches a card operation then; a call that does must
  // not clear the job's busy state and progress out from under it, since no job ran to report either.
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    n8Submenu(stores, be);
    be.holdCartJobs = true;
    be.cartProgress = [{ progress: 4096, total: 65_536 }];
    be.queueBrowse("/saves/held.srm");
    findItem(n8Submenu(stores, be), "n8-dump-sram")!.onSelect!();
    await flush();
    expect(getN8CartJob().busy).toBe(true);
    n8RestoreSram(be, projectCart(stores.project.systems.view())!, "/saves/missing.srm");
    expect(getN8CartJob().busy).toBe(true);
    expect(getN8CartJob().error).toBe("");
    expect(getN8CartJob().bytesDone).toBe(4096);
    be.runCartJob();
    be.pumpJobs();
    await flush();
    expect(getN8CartJob().result).toBe("Backed up the cart's battery");
  } finally {
    env.restore();
  }
});

test("a picked file that cannot be read shows its error when no job holds the row", async () => {
  // The other half of unreadable(): with no job running there is no busy state to leave alone, so the row must show
  // what could not be read rather than the "Ready" label a dropped update() would leave.
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    n8Submenu(stores, be);
    n8RestoreSram(be, projectCart(stores.project.systems.view())!, "/saves/missing.srm");
    const job = getN8CartJob();
    expect(job.busy).toBe(false);
    expect(job.error).toBe("cannot read /saves/missing.srm");
    expect(job.result).toBe("");
  } finally {
    env.restore();
  }
});

test("a handover to another NES keeps the ROM this process booted on the card", async () => {
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    be.seed("/roms/a.nes", nesRom());
    be.seed("/roms/b.nes", nesRom());
    const a = stores.project.systems.addSystem("/roms/a.nes")!;
    const b = stores.project.systems.addSystem("/roms/b.nes")!;
    // Boot a ROM through the menu: its name is what a later battery job reaches, on the cart, not on a node.
    be.seed("/roms/game.nes", nesRom());
    be.queueBrowse("/roms/game.nes");
    findItem(n8Submenu(stores, be), "n8-load-rom")!.onSelect!();
    await flush();
    expect(be.cartRom?.name).toBe("game.nes");
    // The first NES stops driving: the second drives the cart now, so the document places cart-<b> and cart-<a> is
    // gone. The ROM booted on the card is still game.nes - the physical cart's state, not the old node's - so a dump
    // after the handover names it, not the new system's own ROM file.
    stores.project.systems.setRoleConfig(a, "mesen", { driveRealCart: false });
    expect(projectCart(stores.project.systems.view())!.nodeId).toBe(`cart-${b}`);
    be.queueBrowse("/saves/out.srm");
    findItem(n8Submenu(stores, be), "n8-dump-sram")!.onSelect!();
    await flush();
    expect(be.jobRequests[be.jobRequests.length - 1].name).toBe("game.nes");
    expect(be.readFile("/saves/out.srm")).toEqual(be.cartBattery);
  } finally {
    env.restore();
  }
});

test("a running card job disables every row and shows its progress", async () => {
  // The link reads disconnected while a job holds the cart, so `connected` blips false mid-job - `busy` must keep the
  // block, and its progress row, put.
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    n8Submenu(stores, be); // one system, one cart, one document
    be.holdCartJobs = true;
    be.cartProgress = [{ progress: 32_768, total: 65_536 }];
    be.queueBrowse("/saves/held.srm");
    findItem(n8Submenu(stores, be), "n8-dump-sram")!.onSelect!();
    await flush();
    env.cfg.connected = false; // a job holds the cart: the link it borrows reads down

    const rows = n8Submenu(stores, be);
    expect(findItem(rows, "n8-load-rom")!.disabled).toBe(true);
    expect(findItem(rows, "n8-dump-sram")!.disabled).toBe(true);
    expect(findItem(rows, "n8-restore-sram")!.disabled).toBe(true);
    // The link's own rows wait for the job's current step too: a disconnect or a port change mid-transfer would
    // have to wait for it.
    expect(findItem(rows, "n8-port")!.disabled).toBe(true);
    expect(findItem(rows, "n8-connect")!.disabled).toBe(true);
    expect(findItem(rows, "n8-lookahead")!.disabled).toBe(true);
    expect(findItem(rows, "n8-sd-status")!.label).toBe("SD: Reading 50%");

    be.runCartJob();
    be.pumpJobs();
    await flush();
    expect(getN8CartJob().busy).toBe(false);
    env.cfg.connected = true; // the job gave the cart back, so the link is up again
    expect(findItem(n8Submenu(stores, be), "n8-sd-status")!.label).toBe("SD: Backed up the cart's battery");
  } finally {
    env.restore();
  }
});

test("a refused job shows its reason (the cart is not attached)", async () => {
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    n8Submenu(stores, be);
    be.cartConnected = false;
    be.queueBrowse("/saves/none.srm");
    findItem(n8Submenu(stores, be), "n8-dump-sram")!.onSelect!();
    await flush();
    expect(getN8CartJob().busy).toBe(false);
    expect(getN8CartJob().error).toBe("not connected");
  } finally {
    env.restore();
  }
});

test("with no system driving a cart the card rows are absent (the streaming config still shows)", () => {
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    be.seed("/roms/a.gb", gbRom());
    stores.project.systems.addSystem("/roms/a.gb"); // a Game Boy drives no N8
    const sys = stores.project.systems.view()[0];
    const rows = submenuChildren(buildInstanceMenu({ ...ctxOf(stores), system: sys }).items, "inst-n8");
    expect(findItem(rows, "n8-port")).toBeTruthy();
    expect(findItem(rows, "n8-connect")).toBeTruthy();
    expect(findItem(rows, "n8-load-rom")).toBe(undefined);
    expect(findItem(rows, "n8-sd-status")).toBe(undefined);
  } finally {
    env.restore();
  }
});

test("the card rows are hidden until the N8 link is connected (streaming config still shows)", () => {
  const env = installN8({ connected: false, enabled: false });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores, be);
    // Submenu present (an N8 is detected) with the streaming config, but no card rows until you Connect.
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

test("N8 Pro also appears on the start menu (no system loaded), with the streaming config alone", () => {
  // The card rows are the driven cart's node's jobs, and a project with no system places no node: on the start menu
  // there is nothing to load a ROM to the cart through (the project's NES system is that path).
  const env = installN8();
  try {
    const stores = composeAppStores({ backend: new MockBackend("/cfg") });
    const rows = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-n8");
    expect(findItem(rows, "n8-port")).toBeTruthy(); // streaming config
    expect(findItem(rows, "n8-load-rom")).toBe(undefined);
  } finally {
    env.restore();
  }
});

test("the whole N8 Pro submenu is hidden when no N8 is detected (no isN8 port)", () => {
  const env = installN8({ ports: [{ port: "/dev/ttyUSB0", isN8: false }], selectedPort: "" });
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
  const env = installN8({
    ports: [{ port: "/dev/ttyACM0", isN8: true }, { port: "/dev/ttyS0", isN8: false }],
    selectedPort: "",
  });
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

test("the expansion volume is NOT an N8 row: the driven cart's node carries it from the system's knob", () => {
  const env = installN8();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    expect(findItem(n8Submenu(stores, be), "n8-exp-vol")).toBe(undefined);
    // The system's knob reaches the emulated node and the cart node's param alike (buildGraph, the same value twice).
    const sys = stores.project.systems.view()[0];
    const id = sys.id;
    // The knob is a live param: both running nodes are set at once (the emulated core and the cart), and the next
    // document carries the value in both nodes (a settings edit publishes one here). A live edit publishes nothing, so
    // the cart's copy is what moves the real console's master volume.
    stores.project.systems.setRoleConfig(id, "mesen", { expansionVolume: 50 });
    expect(be.paramCalls.slice(-2)).toEqual([
      { nodeId: `sys-${id}`, paramId: "expansionVolume", value: 50 },
      { nodeId: `cart-${id}`, paramId: "expansionVolume", value: 50 },
    ]);
    stores.project.systems.setRoleConfig(id, "mesen", { region: "pal" });
    const doc = be.lastDocument()!;
    expect(doc.nodes.find((n) => n.id === `sys-${id}`)!.params!.expansionVolume).toBe(50);
    expect(doc.nodes.find((n) => n.id === `cart-${id}`)!.params).toEqual({ expansionVolume: 50 });
  } finally {
    env.restore();
  }
});

test("a persisted port that isn't currently detected is still shown, tagged '(not detected)'", () => {
  const env = installN8({ ports: [{ port: "/dev/ttyACM0", isN8: true }], selectedPort: "/dev/ttyOLD" });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    expect(findItem(n8Submenu(stores, be), "n8-port")!.label).toBe("Port: /dev/ttyOLD (not detected)");
  } finally {
    env.restore();
  }
});
