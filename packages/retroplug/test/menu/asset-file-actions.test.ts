// CHARACTERIZATION of the asset Export / Replace file actions, for risa and BlipToaster.
//
// These two consoles' four functions in menuDefs.ts differ by RENAMES ONLY — every validation constant,
// every branch and every error path is byte-identical — and they are about to be collapsed onto one
// AssetMenuSpec. Before this file there was no safety net for that at all: the menu suites assert row
// IDS and labels, the systems suites reach past the menu via setRoleConfig, and no UI test drives asset
// Export/Replace through the file browser. The whole browse -> read -> validate -> build-override ->
// persist path was untested at every layer.
//
// So these tests pin BEHAVIOUR, not structure: what bytes land on disk, what override is recorded, and
// — as much as anything — what happens on the malformed inputs, since a silent `return` is this code's
// entire error-handling strategy and a refactor that turned one into a throw or a write would look green
// everywhere else.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuAction, MenuItem } from "../../ui/screens/menu/menuTree";
import { risaRomFull, blipToasterRom } from "../systems/fixtures";

// Export browses fire-and-forget; flush the microtask chain browseThen kicks off.
const flush = async (): Promise<void> => {
  for (let i = 0; i < 8; i++) await Promise.resolve();
};

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
    beginSongImport: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
  };
}

const kids = (items: MenuItem[], id: string): MenuItem[] => {
  const sm = items.find((i) => i.id === id);
  return sm && sm.kind === "submenu" ? sm.children ?? [] : [];
};
const verbs = (items: MenuItem[], id: string): MenuAction[] => {
  const row = items.find((i) => i.id === id);
  return row && row.kind === "actionCycler" ? row.actions ?? [] : [];
};

/** The two consoles, driven through the identical seam — which is the point: every case below runs twice. */
const CONSOLES = [
  { id: "risa", role: "risa-assets", rom: risaRomFull, path: "/roms/full.nes", kitExt: ".rkit" },
  { id: "bliptoaster", role: "bliptoaster-assets", rom: blipToasterRom, path: "/roms/synth.nes", kitExt: ".rkit" },
] as const;

type Harness = {
  be: MockBackend;
  stores: AppStores;
  /** A verb on an asset row, e.g. fire("theme", 0, "export"). */
  fire(kind: string, slot: number, verb: string): Promise<void>;
  overrides(): { type: string; slot: number; name?: string; path?: string; theme?: unknown }[];
};

function boot(c: (typeof CONSOLES)[number]): Harness {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed(c.path, c.rom());
  stores.project.systems.addSystem(c.path);
  // Re-read the system every time rather than capturing it: writeOverrides ends in reloadSystem, which
  // rebuilds in place under a NEW id, so a captured id goes stale after the first Replace. The menu has
  // the same property — it is handed a fresh SystemView on every render.
  const sys = () => stores.project.systems.view()[0];
  const items = (): MenuItem[] => buildInstanceMenu({ ...ctxOf(stores), system: sys() }).items;
  const plural = (kind: string): string => `${c.id}-${kind}s`;
  return {
    be,
    stores,
    async fire(kind, slot, verb) {
      const sub = kids(kids(items(), `inst-${c.id}`), plural(kind));
      const v = verbs(sub, `${c.id}-${kind}-${slot}`).find((a) => a.id === `${c.id}-${kind}-${slot}-${verb}`);
      expect(v != null, `${c.id} ${kind} ${slot} has a ${verb} verb`).toBeTruthy();
      v!.onSelect!();
      await flush(); // browseThen resolves through a promise chain — for Replace as much as for Export
    },
    overrides() {
      const cfg = sys().roles.find((r) => r.kind === c.role)?.config;
      const raw = (cfg as { overrides?: unknown } | undefined)?.overrides;
      return Array.isArray(raw) ? (raw as ReturnType<Harness["overrides"]>) : [];
    },
  };
}

for (const c of CONSOLES) {
  // ---- Export -------------------------------------------------------------------------------------

  test(`${c.id}: Export theme writes the base ROM's theme as readable .rit JSON`, async () => {
    const h = boot(c);
    h.be.queueBrowse("/out/theme0.rit");
    await h.fire("theme", 0, "export");

    const written = h.be.readFile("/out/theme0.rit");
    expect(written != null, "a file was written").toBeTruthy();
    const parsed = JSON.parse(new TextDecoder().decode(written!)) as Record<string, unknown>;
    expect(typeof parsed, "the .rit is a JSON object").toBe("object");
    // Trailing newline: these are hand-editable files, and losing it is the kind of thing a rewrite does.
    expect(new TextDecoder().decode(written!).endsWith("\n"), "trailing newline").toBeTruthy();
  });

  test(`${c.id}: Export font/kit writes the base ROM's bank verbatim`, async () => {
    const h = boot(c);
    h.be.queueBrowse("/out/font0.chr");
    await h.fire("font", 0, "export");
    const font = h.be.readFile("/out/font0.chr");
    expect(font != null, "a font bank was written").toBeTruthy();
    expect(font!.length, "a .chr is exactly one 8 KB CHR bank").toBe(0x2000);
  });

  // Kits and fonts are BOTH linked by path, and both export branches read the link. Covering only one
  // leaves the other free to silently fall back to the base ROM.
  test(`${c.id}: Export of an OVERRIDDEN kit returns the linked bank, not the ROM's`, async () => {
    const h = boot(c);
    // Round-trip the cart's own kit 0 so the bank is guaranteed populated for this build, then mark it.
    h.be.queueBrowse("/out/base.rkit");
    await h.fire("kit", 0, "export");
    const base = h.be.readFile("/out/base.rkit");
    expect(base != null, "the base kit exported").toBeTruthy();
    const linked = new Uint8Array(base!);
    linked[linked.length - 1] = 0x5a; // a byte the base bank does not end on
    h.be.seed("/link/custom.rkit", linked);
    h.be.queueBrowse("/link/custom.rkit");
    await h.fire("kit", 0, "replace");
    expect(h.overrides().some((o) => o.type === "kit" && o.slot === 0), "the kit override took").toBeTruthy();

    h.be.queueBrowse("/out/kit-roundtrip.rkit");
    await h.fire("kit", 0, "export");
    const out = h.be.readFile("/out/kit-roundtrip.rkit");
    expect(out != null, "the override round-tripped").toBeTruthy();
    expect(out![out!.length - 1], "the last byte came from the linked bank").toBe(0x5a);
  });

  test(`${c.id}: Export of an OVERRIDDEN font returns the linked file, not the ROM's bank`, async () => {
    const h = boot(c);
    const linked = new Uint8Array(0x2000).fill(0xab);
    h.be.seed("/link/custom.chr", linked);
    h.be.queueBrowse("/link/custom.chr");
    await h.fire("font", 0, "replace");

    h.be.queueBrowse("/out/roundtrip.chr");
    await h.fire("font", 0, "export");
    const out = h.be.readFile("/out/roundtrip.chr");
    expect(out != null, "the override round-tripped").toBeTruthy();
    expect(out![0], "byte 0 came from the linked file").toBe(0xab);
  });

  // The divergence from LSDj, pinned deliberately. LSDj falls back to the base ROM asset when an
  // override's linked file has moved; these two return null and write NOTHING, silently. Pinned as-is so
  // the collapse is provably behaviour-preserving; changing it is a separate, deliberate commit.
  test(`${c.id}: Export of an override whose linked file is GONE writes nothing (no fallback)`, async () => {
    const h = boot(c);
    const linked = new Uint8Array(0x2000).fill(0xcd);
    h.be.seed("/link/moved.chr", linked);
    h.be.queueBrowse("/link/moved.chr");
    await h.fire("font", 0, "replace");
    h.be.deleteFile("/link/moved.chr"); // the user moved it after linking

    h.be.queueBrowse("/out/stale.chr");
    await h.fire("font", 0, "export");
    expect(h.be.readFile("/out/stale.chr"), "nothing is written, and nothing is said").toBe(null);
  });

  // ---- Replace ------------------------------------------------------------------------------------

  test(`${c.id}: Replace font records an override BY PATH and leaves the .nes on disk untouched`, async () => {
    const h = boot(c);
    const before = h.be.readFile(c.path)!;
    h.be.seed("/link/good.chr", new Uint8Array(0x2000).fill(7));
    h.be.queueBrowse("/link/good.chr");
    await h.fire("font", 0, "replace");

    const ov = h.overrides().find((o) => o.type === "font" && o.slot === 0);
    expect(ov != null, "an override was recorded").toBeTruthy();
    expect(ov!.path, "fonts are linked by path, never inlined").toBe("/link/good.chr");
    expect([...h.be.readFile(c.path)!], "the base ROM is never written").toEqual([...before]);
  });

  test(`${c.id}: Replace theme records the parsed theme INLINE, not a path`, async () => {
    const h = boot(c);
    // Round-trip the cart's own theme 0 so the .rit is guaranteed well-formed for this build.
    h.be.queueBrowse("/out/t.rit");
    await h.fire("theme", 0, "export");
    const rit = h.be.readFile("/out/t.rit")!;
    h.be.seed("/link/mine.rit", rit);
    h.be.queueBrowse("/link/mine.rit");
    await h.fire("theme", 1, "replace");

    const ov = h.overrides().find((o) => o.type === "theme" && o.slot === 1);
    expect(ov != null, "an override was recorded").toBeTruthy();
    expect(ov!.theme != null, "themes are carried inline").toBeTruthy();
    expect(ov!.path, "and never by path").toBe(undefined);
  });

  // Every rejection path. A silent `return` is this code's whole error strategy, so each of these
  // asserts the ABSENCE of an override — the only observable the caller gets.
  test(`${c.id}: Replace rejects a malformed .rit, a wrong-size .chr and a wrong-size kit, silently`, async () => {
    const h = boot(c);

    h.be.seed("/link/bad.rit", new TextEncoder().encode("{ not json"));
    h.be.queueBrowse("/link/bad.rit");
    await h.fire("theme", 0, "replace");
    expect(h.overrides().length, "malformed JSON records nothing").toBe(0);

    h.be.seed("/link/short.chr", new Uint8Array(0x1000)); // half a bank
    h.be.queueBrowse("/link/short.chr");
    await h.fire("font", 0, "replace");
    expect(h.overrides().length, "a wrong-size .chr records nothing").toBe(0);

    h.be.seed("/link/empty.rkit", new Uint8Array(0x2000)); // right size, all zero = unpopulated
    h.be.queueBrowse("/link/empty.rkit");
    await h.fire("kit", 0, "replace");
    expect(h.overrides().length, "an unpopulated kit bank records nothing").toBe(0);
  });

  test(`${c.id}: Replace on a slot that already has an override replaces it rather than stacking`, async () => {
    const h = boot(c);
    h.be.seed("/link/a.chr", new Uint8Array(0x2000).fill(1));
    h.be.seed("/link/b.chr", new Uint8Array(0x2000).fill(2));
    h.be.queueBrowse("/link/a.chr");
    await h.fire("font", 0, "replace");
    h.be.queueBrowse("/link/b.chr");
    await h.fire("font", 0, "replace");

    const fonts = h.overrides().filter((o) => o.type === "font" && o.slot === 0);
    expect(fonts.length, "one override per slot").toBe(1);
    expect(fonts[0].path).toBe("/link/b.chr");
  });

  test(`${c.id}: a cancelled browse records nothing and writes nothing`, async () => {
    const h = boot(c);
    h.be.queueBrowse(null); // the user cancelled the dialog
    await h.fire("font", 0, "replace");
    expect(h.overrides().length, "cancel is not an edit").toBe(0);

    h.be.queueBrowse(null);
    await h.fire("font", 0, "export");
    expect(h.be.readFile("/out/never.chr")).toBe(null);
  });
}
