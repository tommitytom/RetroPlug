// The Recent rows and the smsggdj Songs > Load row, at the menu level: what an smsggdj project's rows
// look like, that a row reopens the project WITH its song, and that the Songs menu's Load goes through
// the store's song request - so on a cart that is still booting it waits instead of writing into RAM the
// boot is about to erase (or cold-booting the cart blank, which is what the live path's refusal used to
// fall back to).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, buildInstanceMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "../../ui/screens/menu/menuTree";
import { buildSav, SMDJ4_BLOCK_LEN } from "../../src/smsggdj/codec/sav";
import { resolveSmsggdjLayout } from "../../src/smsggdj/runtime/layout";
import { smsggdjRom, smsggdjRam } from "../systems/fixtures";

const findItem = (items: MenuItem[], id: string): MenuItem | undefined => items.find((i) => i.id === id);
const submenuChildren = (items: MenuItem[], id: string): MenuItem[] => findItem(items, id)?.children ?? [];
const PRINTABLE = /^[\x20-\x7e]+$/;

function ctxOf(stores: AppStores, loadProject: MenuContext["loadProject"] = () => {}): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject,
    loadRomAsProject: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
    beginSongImport: () => {},
  };
}

const CART = 32 * 1024;
const block = (tag: number): Uint8Array => {
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
  return b;
};
const layout = resolveSmsggdjLayout("0.45")!;

test("an smsggdj project's Recent rows: the song leads, the cart's identity follows, one row per song", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/music/sms.rplg", "{}"); // on disk, so the rows are not drawn as missing
  const picked: [string, string | undefined][] = [];
  const rows = () => submenuChildren(buildStartMenu(ctxOf(stores, (p, s) => picked.push([p, s]))).items, "start-recent");

  // The project row a blank boot records, then the song the user loaded: one row, the song's.
  stores.recent.add("/music/sms.rplg", "smsggdj.sav [smsggdj]");
  expect(rows().map((r) => r.label)).toEqual(["smsggdj.sav [smsggdj]"]);
  stores.recent.add("/music/sms.rplg", "smsggdj.sav [smsggdj]", "DEMO");
  expect(rows().map((r) => r.label)).toEqual(["DEMO - smsggdj.sav [smsggdj]"]);
  stores.recent.add("/music/sms.rplg", "smsggdj.sav [smsggdj]", "INTRO");
  expect(rows().map((r) => r.label)).toEqual(["INTRO - smsggdj.sav [smsggdj]", "DEMO - smsggdj.sav [smsggdj]"]);
  for (const r of rows()) expect(PRINTABLE.test(r.label)).toBe(true); // nothing a font cannot draw

  // Picking a row reopens the project WITH that row's song - the song travels as the request the store
  // settles once the cart is up, which is why the row hands it over rather than loading it.
  findItem(rows(), "recent-1")!.onSelect!();
  expect(picked).toEqual([["/music/sms.rplg", "DEMO"]]);
});

/** A composed app with an smsggdj cart whose work RAM is `ram`, and the Songs > row `index` > Load leaf. */
function smsSongsMenu(ram: Parameters<typeof smsggdjRam>[0]) {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  stores.project.systems.loadRom("/roms/smsggdj.sms");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!);
  be.setRam(id, smsggdjRam(ram));
  const load = (index: number): MenuItem => {
    const sys = stores.project.systems.view().find((s) => s.id === id)!;
    const inst = buildInstanceMenu({ ...ctxOf(stores), system: sys }).items;
    const songs = submenuChildren(submenuChildren(inst, "inst-smsggdj"), "smsggdj-songs");
    return findItem(submenuChildren(songs, `smsggdj-song-${index}`), `smsggdj-song-${index}-load`)!;
  };
  return {
    be, stores, id, load,
    writes: () => be.log.filter((m) => m === "writeRam").length,
    working: () => be.readRam(id)!.subarray(layout.song, layout.song + SMDJ4_BLOCK_LEN),
  };
}

test("Songs > Load on a running smsggdj cart loads right there, live - no reboot, no wait", () => {
  const m = smsSongsMenu({ booted: true });
  const builds = m.be.constructCalls.length;
  const load = m.load(1);
  expect(load.kind).toBe("action"); // a clean working song: no guard
  load.onSelect!();
  expect(m.working()).toEqual(block(2)); // BETA, this very call
  expect(m.stores.project.hasSongRequest()).toBe(false); // settled inline
  expect(m.be.constructCalls.length).toBe(builds); // no cold boot
});

test("Songs > Load on an smsggdj cart that is still booting parks the request and settles it later", () => {
  // Before: the live load declined (rightly - the boot would have erased it), and the menu fell back to
  // the cold-boot path, which on v0.45 boots BLANK. Now the row is a request the frame tick settles.
  const m = smsSongsMenu({ booted: false });
  const builds = m.be.constructCalls.length;
  m.load(1).onSelect!();
  expect(m.stores.project.hasSongRequest()).toBe(true);
  expect(m.writes()).toBe(0); // not a byte into a booting cart
  expect(m.be.constructCalls.length).toBe(builds); // and no cold boot either
  expect(m.working()).toEqual(new Uint8Array(SMDJ4_BLOCK_LEN));

  expect(m.stores.project.settleSong()).toBe("waiting"); // the tick, while the cart boots
  m.be.setRam(m.id, smsggdjRam({ booted: true }));
  expect(m.stores.project.settleSong()).toBe("loaded"); // ...and the tick that lands it
  expect(m.working()).toEqual(block(2));
});
