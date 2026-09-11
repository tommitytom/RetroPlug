// The Recent list and a tracker cart that cannot yet say what it holds.
//
// A project's own Recent row used to be recorded the instant it became the open project (save / load /
// adopt), with whatever song the cart reported. For smsggdj that instant is before the cart has booted:
// its working song is work RAM, and the RAM was the boot sequence's, so the row went in SONGLESS and the
// song watcher added the real row a few seconds later - Recent showed both. Now the row is OWED while
// the cart is booting and paid by syncRecent on the first tick it is up: once, and correct on first
// appearance - songless only when the cart is genuinely blank, and superseded the moment a song row
// exists for that project (recentList's rule).
//
// mGB and LSDj are the regression net: their rows are recorded at once, exactly as before, because there
// is nothing to wait for.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { buildSav, SMDJ4_BLOCK_LEN } from "../../src/smsggdj/codec/sav";
import { savFrom, type SavInput } from "../../src/lsdjSav";
import { smsggdjRom, smsggdjRam, lsdjRom, gbRomBattery } from "../systems/fixtures";

const CART = 32 * 1024;
const block = (tag: number): Uint8Array => {
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
  return b;
};
const twoSongs = (): Uint8Array => buildSav([{ block: block(1), name: "DEMO" }, { block: block(2), name: "INTRO" }], CART)!;

/** An smsggdj cart in a fresh project - NOT yet saved, so each test picks how the project comes to be. */
function smsCart() {
  const be = new MockBackend("/cfg");
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const id = project.systems.addSystem("/roms/smsggdj.sms")!;
  be.setSram(id, twoSongs());
  const rows = () => recent.view().map((v) => ({ song: v.song, label: v.label }));
  const cart = (opts: Parameters<typeof smsggdjRam>[0] = {}) => be.setRam(project.systems.primary()!.id, smsggdjRam(opts));
  return { be, recent, project, id, rows, cart };
}

test("save: the row is owed while the cart boots, and paid - songless - once it is up and blank", () => {
  const { project, rows, cart } = smsCart();
  cart({ booted: false });
  expect(project.save("/proj/sms.rplg")).toBeTruthy();
  expect(rows()).toEqual([]); // not "smsggdj.sav [smsggdj]" on its own: nothing yet
  expect(project.currentPath()).toBe("/proj/sms.rplg"); // the project IS open; only the row waits

  expect(project.syncRecent()).toBe(false); // the watcher's tick: still booting
  expect(rows()).toEqual([]);

  cart({ booted: true }); // ints_on = 1, and the cart holds song_new's blank song: genuinely no song
  expect(project.syncRecent()).toBe(true);
  expect(rows()).toEqual([{ song: undefined, label: "smsggdj.sav [smsggdj]" }]); // the honest songless row
  expect(project.syncRecent()).toBe(false); // paid once; the steady state is silent
  expect(rows().length).toBe(1);
});

test("a cart that comes up WITH a song (v0.46 boot_autoload) records the song row directly", () => {
  // No songless row is ever written for it - the first row Recent shows is the right one.
  const { project, rows, cart } = smsCart();
  cart({ booted: false });
  project.save("/proj/sms.rplg");
  expect(rows()).toEqual([]);
  cart({ booted: true, block: block(2), name: "INTRO" });
  expect(project.syncRecent()).toBe(true);
  expect(rows()).toEqual([{ song: "INTRO", label: "smsggdj.sav [smsggdj]" }]);
});

test("a song loaded into a blank-booted project supersedes its songless row, and rows stay one per song", () => {
  const { project, rows, cart } = smsCart();
  cart({ booted: true }); // a cart that is already up when the project is saved: recorded at once
  project.save("/proj/sms.rplg");
  expect(rows()).toEqual([{ song: undefined, label: "smsggdj.sav [smsggdj]" }]);

  cart({ block: block(1), name: "DEMO" }); // the user loads DEMO on the cart's own FILES screen
  expect(project.syncRecent()).toBe(true);
  expect(rows()).toEqual([{ song: "DEMO", label: "smsggdj.sav [smsggdj]" }]); // the placeholder is gone

  cart({ block: block(2), name: "INTRO" });
  expect(project.syncRecent()).toBe(true);
  expect(rows().map((r) => r.song)).toEqual(["INTRO", "DEMO"]);
  cart({ block: block(1), name: "DEMO" }); // ...and back: moved up, not duplicated
  expect(project.syncRecent()).toBe(true);
  expect(rows().map((r) => r.song)).toEqual(["DEMO", "INTRO"]);
  expect(project.syncRecent()).toBe(false);
});

test("a settled song request records its row itself, and the owed project row then adds nothing", () => {
  // The Recent-row flow end to end at the store level: project loads (row owed), the row's song is
  // requested, the cart boots, the request settles - ONE row, the song row, and the watcher's later tick
  // finds nothing left to pay.
  const { project, rows, cart } = smsCart();
  cart({ booted: false });
  project.save("/proj/sms.rplg");
  project.requestSong({ name: "DEMO", confirmed: false });
  expect(project.settleSong()).toBe("waiting");
  expect(rows()).toEqual([]);
  cart({ booted: true });
  expect(project.settleSong()).toBe("loaded");
  expect(rows()).toEqual([{ song: "DEMO", label: "smsggdj.sav [smsggdj]" }]);
  expect(project.syncRecent()).toBe(false); // the owed row resolves to (path, DEMO): already there
  expect(rows().length).toBe(1);
});

test("load: the row is owed across a project load, and the new cart is read on its own terms", () => {
  const { be, project, rows, cart } = smsCart();
  cart({ booted: true, block: block(1), name: "DEMO" });
  project.save("/proj/sms.rplg");
  expect(rows().map((r) => r.song)).toEqual(["DEMO"]);

  // Reopen it. The rebuilt core publishes no RAM yet in the mock (as a real core's is the boot's), so
  // the load records nothing and the DEMO row stands alone - no songless row appears above it.
  project.newProject();
  expect(project.load("/proj/sms.rplg").kind).toBe("loaded");
  expect(rows().map((r) => r.song)).toEqual(["DEMO"]);
  expect(project.syncRecent()).toBe(false);

  // The new cart comes up blank (v0.45): its owed row is songless, and a songless row for a project that
  // has song rows is nothing new - the list is unchanged.
  const id = project.systems.primary()!.id;
  be.setSram(id, twoSongs());
  cart({ booted: true });
  expect(project.syncRecent()).toBe(false);
  expect(rows().map((r) => r.song)).toEqual(["DEMO"]);
});

test("adoptRomProject and export follow the same rule", () => {
  const { be, project, rows, cart } = smsCart();
  cart({ booted: false });
  project.adoptRomProject("/roms/smsggdj.sms"); // writes the sibling .rplg via save
  expect(be.fileExists("/roms/smsggdj.rplg")).toBe(true);
  expect(rows()).toEqual([]);
  cart({ booted: true });
  expect(project.syncRecent()).toBe(true);
  expect(rows()).toEqual([{ song: undefined, label: "smsggdj.sav [smsggdj]" }]);

  const e = smsCart();
  e.cart({ booted: false });
  expect(e.project.export("/proj/sms.rplg.zip")).toBe(true);
  expect(e.rows()).toEqual([]);
  e.cart({ booted: true, block: block(2), name: "INTRO" });
  expect(e.project.syncRecent()).toBe(true);
  expect(e.rows().map((r) => r.song)).toEqual(["INTRO"]);
});

test("the owed row is forgotten with the project it was owed for", () => {
  const { project, rows, cart } = smsCart();
  cart({ booted: false });
  project.save("/proj/sms.rplg");
  project.newProject();
  expect(project.syncRecent()).toBe(false); // no path, no cart, nothing owed
  expect(rows()).toEqual([]);
});

test("mGB and LSDj record their rows at once - readiness is only ever the smsggdj cart's wait", () => {
  const be = new MockBackend("/cfg");
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, buildAppRegistry());
  be.seed("/roms/game.gb", gbRomBattery());
  project.systems.addSystem("/roms/game.gb");
  expect(project.save("/proj/game.rplg")).toBeTruthy();
  expect(recent.view().map((v) => [v.song, v.label])).toEqual([[undefined, "game.sav [game]"]]);

  const SONG = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
  const lb = new MockBackend("/cfg");
  const lrecent = new RecentStore(lb);
  const lproject = new ProjectStore(lb, lrecent, buildAppRegistry());
  lb.seed("/roms/lsdj.gb", lsdjRom("LSDJ-V9.4.2"));
  const id = lproject.systems.addSystem("/roms/lsdj.gb")!;
  lb.setSram(id, savFrom({ activeProjectIndex: 0, projects: [{ name: "GRUB", version: 0, song: SONG }] } as SavInput));
  expect(lproject.save("/proj/lsdj.rplg")).toBeTruthy();
  expect(lrecent.view().map((v) => v.song)).toEqual(["GRUB"]); // the battery names the song from frame one
});
