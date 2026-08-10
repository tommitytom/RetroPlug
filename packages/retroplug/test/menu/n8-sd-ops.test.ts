// The Settings > N8 Pro SD-card ops (Load ROM / Dump / Restore SRAM). The rows appear only when the host
// exposes the N8 SD seam (the __rp_n8* hooks bindN8Hooks installs); each browses a local file then calls its
// native hook, is disabled while a job runs, and the read-only SD row renders the polled status snapshot. We
// stub the globals (the native worker isn't present in the pure-TS test host) and drive the browse through
// the MockBackend, exactly like leaves.test does for the file-dialog leaves.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";

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
function n8Submenu(stores: AppStores): MenuItem[] {
  const settings = submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings");
  return submenuChildren(settings, "set-n8");
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
}

// Install the __rp_* N8 hooks the UI reads (getN8Config makes the submenu appear; getN8SdStatus + the three
// op hooks make the SD rows appear + record their calls). Returns a spy record + a restore().
function installN8(sd: Sd | null): { calls: Calls; restore: () => void } {
  const g = globalThis as Record<string, unknown>;
  const calls: Calls = { load: [], dump: [], restore: [] };
  const saved = {
    cfg: g.__rp_getN8Config,
    sd: g.__rp_getN8SdStatus,
    load: g.__rp_n8LoadRom,
    dump: g.__rp_n8DumpSram,
    restore: g.__rp_n8RestoreSram,
  };
  g.__rp_getN8Config = () => ({ ports: [{ port: "/dev/ttyACM0", isN8: true }], selectedPort: "/dev/ttyACM0", connected: false, enabled: false, lookaheadMs: 10, bytes: 0, error: "" });
  g.__rp_getN8SdStatus = sd === null ? undefined : () => sd;
  g.__rp_n8LoadRom = (p: string) => calls.load.push(p);
  g.__rp_n8DumpSram = (p: string) => calls.dump.push(p);
  g.__rp_n8RestoreSram = (p: string) => calls.restore.push(p);
  return {
    calls,
    restore: () => {
      g.__rp_getN8Config = saved.cfg;
      g.__rp_getN8SdStatus = saved.sd;
      g.__rp_n8LoadRom = saved.load;
      g.__rp_n8DumpSram = saved.dump;
      g.__rp_n8RestoreSram = saved.restore;
    },
  };
}

test("Settings > N8 Pro shows the SD ops, and each browses a local file then calls its native hook", async () => {
  const env = installN8({ busy: false, op: "", done: false, version: 0 });
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    const rows = n8Submenu(stores);
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
    findItem(n8Submenu(stores), "n8-dump-sram")!.onSelect!();
    await flush();
    expect(env.calls.dump).toEqual(["/saves/out.srm"]);
    expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].saving).toBe(true);

    // Restore SRAM: a read dialog, then __rp_n8RestoreSram with the pick.
    be.queueBrowse("/saves/in.srm");
    findItem(n8Submenu(stores), "n8-restore-sram")!.onSelect!();
    await flush();
    expect(env.calls.restore).toEqual(["/saves/in.srm"]);
    expect(!!be.fileBrowserCalls[be.fileBrowserCalls.length - 1].saving).toBe(false);
  } finally {
    env.restore();
  }
});

test("the SD rows (and Connect) are disabled while a job is busy, and the status row shows progress", () => {
  const env = installN8({ busy: true, op: "load", phase: "Uploading", bytesDone: 45, bytesTotal: 100, done: false, version: 3 });
  try {
    const stores = composeAppStores({ backend: new MockBackend("/cfg") });
    const rows = n8Submenu(stores);
    expect(findItem(rows, "n8-load-rom")!.disabled).toBe(true);
    expect(findItem(rows, "n8-dump-sram")!.disabled).toBe(true);
    expect(findItem(rows, "n8-restore-sram")!.disabled).toBe(true);
    expect(findItem(rows, "n8-connect")!.disabled).toBe(true); // streaming Connect is disabled while an SD op runs
    expect(findItem(rows, "n8-sd-status")!.label).toBe("SD: Uploading 45%");
  } finally {
    env.restore();
  }
});

test("the SD status row shows the last result when idle", () => {
  const env = installN8({ busy: false, op: "dump", done: true, result: "Dumped 64 KB to out.srm", version: 5 });
  try {
    const stores = composeAppStores({ backend: new MockBackend("/cfg") });
    expect(findItem(n8Submenu(stores), "n8-sd-status")!.label).toBe("SD: Dumped 64 KB to out.srm");
  } finally {
    env.restore();
  }
});

test("the SD rows are absent when the host lacks the SD seam (streaming-only submenu still builds)", () => {
  const env = installN8(null); // __rp_getN8SdStatus undefined -> hasN8Sd() false; streaming config still present
  try {
    const stores = composeAppStores({ backend: new MockBackend("/cfg") });
    const rows = n8Submenu(stores);
    expect(findItem(rows, "n8-load-rom")).toBe(undefined);
    expect(findItem(rows, "n8-sd-status")).toBe(undefined);
    expect(findItem(rows, "n8-status")).toBeTruthy(); // the streaming status row (hasN8()) still there
  } finally {
    env.restore();
  }
});
