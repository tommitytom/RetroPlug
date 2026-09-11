// The song REQUEST: one mechanism for the two places a song load starts - a Recent row (by name) and a
// Songs-menu row (by index) - parked in the ProjectStore and settled when the cart can take it.
//
// Both used to do the load themselves, immediately. On a console whose working song is work RAM
// (smsggdj) "immediately" after a project load is before the cart has booted: the write landed, the
// cart's `song_new` ran a moment later, and the song was gone - while the caller believed it had
// loaded. A Recent load only ever worked because the discard prompt in front of it cost the user a
// couple of seconds. Here the request waits on the cart's own readiness latch, is guarded at APPLY
// time (when the working song is real), and records its Recent row itself.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore, type SongSettle } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { buildSav, SMDJ4_BLOCK_LEN } from "../../src/smsggdj/codec/sav";
import { resolveSmsggdjLayout } from "../../src/smsggdj/runtime/layout";
import { lsdjSongCatalog } from "../../src/tracker";
import { savFrom, loadSongToWorking, type SavInput } from "../../src/lsdjSav";
import { smsggdjRom, smsggdjRam, lsdjRom, gbRomBattery } from "../systems/fixtures";

const CART = 32 * 1024;
const block = (tag: number): Uint8Array => {
  const b = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
  return b;
};
const twoSongs = (): Uint8Array => buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], CART)!;
const layout = resolveSmsggdjLayout("0.45")!;

/** An smsggdj project, saved (so rows have a path to be recorded against), whose cart holds `ram` - or,
 *  with no `ram`, publishes no work RAM at all (the mock's default), which reads as "still booting". */
function smsProject(ram?: Parameters<typeof smsggdjRam>[0], sav: Uint8Array = twoSongs()) {
  const be = new MockBackend("/cfg");
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, buildAppRegistry());
  be.seed("/roms/smsggdj.sms", smsggdjRom("0.45"));
  const id = project.systems.addSystem("/roms/smsggdj.sms")!;
  be.setSram(id, sav);
  if (ram) be.setRam(id, smsggdjRam(ram));
  expect(project.save("/proj/sms.rplg")).toBeTruthy();
  const ramNow = () => be.readRam(id)!;
  return {
    be, recent, project, id,
    boot: (opts: Parameters<typeof smsggdjRam>[0] = {}) => be.setRam(id, smsggdjRam(opts)),
    writes: () => be.log.filter((m) => m === "writeRam").length,
    working: () => ramNow().subarray(layout.song, layout.song + SMDJ4_BLOCK_LEN),
    // The codec pads a directory name with zeros and the cart with spaces; a name is what is left.
    name: () => String.fromCharCode(...ramNow().subarray(layout.name, layout.name + layout.nameLen)).replace(/\0+$/, "").trim(),
    songs: () => recent.view().map((v) => v.song),
  };
}

const kind = (s: SongSettle): string => (typeof s === "string" ? s : s.kind);

test("a request made before the cart has booted waits, and loads on the first tick the cart is up", () => {
  const p = smsProject({ booted: false }); // work RAM published, but the boot's, not the cart's
  p.project.requestSong({ name: "BETA", confirmed: false });
  expect(p.project.hasSongRequest()).toBe(true);

  // Tick after tick: waiting, and not a byte written into a cart that would erase it.
  for (let i = 0; i < 5; i++) expect(p.project.settleSong()).toBe("waiting");
  expect(p.writes()).toBe(0);
  expect(p.project.hasSongRequest()).toBe(true);
  expect(p.songs()).toEqual([]); // the project's own row is owed too (see recent-tracker) - nothing yet

  p.boot(); // ints_on = 1: the blank song is now the cart's
  expect(p.project.settleSong()).toBe("loaded");
  expect(p.working()).toEqual(block(2)); // BETA, in work RAM
  expect(p.name()).toBe("BETA");
  expect(p.project.hasSongRequest()).toBe(false);
  expect(p.songs()).toEqual(["BETA"]); // the row is recorded by the settle, by name, at once
  expect(p.project.settleSong()).toBe("idle"); // nothing parked any more
});

test("no work RAM published at all is 'still booting', not 'ready' - the safe polarity for a write", () => {
  const p = smsProject(); // the mock publishes nothing until a test sets RAM: exactly a core with no snapshot
  p.project.requestSong({ name: "ALPHA", confirmed: false });
  expect(p.project.settleSong()).toBe("waiting");
  expect(p.writes()).toBe(0);
  p.boot();
  expect(p.project.settleSong()).toBe("loaded");
  expect(p.working()).toEqual(block(1));
});

test("a Recent row's request is guarded when it APPLIES: discard asks once, and the answer decides", () => {
  // A running cart with real unsaved work: BETA loaded, then edited (the cart's own flag set, and the
  // block matching no slot). The guard has to run here - against this - and not at the moment the project
  // loaded, when the cart held nothing.
  const edited = block(2);
  edited[9] ^= 0xff;
  const p = smsProject({ block: edited, name: "BETA", edited: true });

  p.project.requestSong({ name: "ALPHA", confirmed: false });
  const asked = p.project.settleSong();
  expect(kind(asked)).toBe("discard");
  expect(asked).toEqual({ kind: "discard", song: "ALPHA", working: "BETA" });
  // Asked ONCE: while the prompt is up, every further tick is a plain wait, and nothing is written.
  expect(p.project.settleSong()).toBe("waiting");
  expect(p.project.settleSong()).toBe("waiting");
  expect(p.writes()).toBe(0);

  // "Keep current song": the request is withdrawn, the edit survives.
  p.project.cancelSong();
  expect(p.project.settleSong()).toBe("idle");
  expect(p.working()).toEqual(edited);
  expect(p.songs()).toEqual(["BETA"]); // the save recorded the cart's song; nothing since

  // "Discard & load": agreeing is what lets the next settle apply it.
  p.project.requestSong({ name: "ALPHA", confirmed: false });
  expect(kind(p.project.settleSong())).toBe("discard");
  p.project.confirmSong();
  expect(p.project.settleSong()).toBe("loaded");
  expect(p.working()).toEqual(block(1));
  expect(p.name()).toBe("ALPHA");
  expect(p.songs()).toEqual(["ALPHA", "BETA"]);
});

test("a Songs-menu request arrives confirmed (the menu guarded its own row) and loads the exact slot", () => {
  const edited = block(2);
  edited[9] ^= 0xff;
  const p = smsProject({ block: edited, name: "BETA", edited: true }); // dirty - a name request WOULD ask
  p.project.requestSong({ index: 0, confirmed: true });
  expect(p.project.settleSong()).toBe("loaded"); // no prompt: the menu's loadGuard already had the answer
  expect(p.working()).toEqual(block(1));
  expect(p.songs()[0]).toBe("ALPHA");

  // By INDEX is exact under duplicate names, where a name can only mean the first.
  const dup = buildSav([{ block: block(1), name: "DUP" }, { block: block(2), name: "DUP" }], CART)!;
  const d = smsProject({}, dup);
  d.project.requestSong({ index: 1, confirmed: true });
  expect(d.project.settleSong()).toBe("loaded");
  expect(d.working()).toEqual(block(2));
  // ...whereas by NAME a "DUP" is already the working song, whichever DUP it is: the rows are
  // indistinguishable in the list too, and a name request does not pretend to tell them apart.
  d.project.requestSong({ name: "DUP", confirmed: true });
  expect(d.project.settleSong()).toBe("loaded");
  expect(d.working()).toEqual(block(2)); // untouched
  d.project.requestSong({ index: 0, confirmed: true });
  expect(d.project.settleSong()).toBe("loaded");
  expect(d.working()).toEqual(block(1)); // the index says exactly which
});

test("re-picking the working song by name is a no-op that still records the row; by index it reloads", () => {
  // A Recent row for the song already loaded must not throw the user's edits away - the row means "open
  // this song", and it is open. A menu row for it is the documented way to reload the slot's copy.
  const edited = block(2);
  edited[9] ^= 0xff;
  const p = smsProject({ block: edited, name: "BETA", edited: true });
  p.project.requestSong({ name: "BETA", confirmed: false });
  expect(p.project.settleSong()).toBe("loaded");
  expect(p.writes()).toBe(0); // nothing written
  expect(p.working()).toEqual(edited); // the edit is kept
  expect(p.songs()).toEqual(["BETA"]); // the row moved up (it was already the front row)

  p.project.requestSong({ index: 1, confirmed: true });
  expect(p.project.settleSong()).toBe("loaded");
  expect(p.writes() > 0).toBe(true);
  expect(p.working()).toEqual(block(2)); // the slot's copy, edit gone - as asked
});

test("a song that is gone, or a cart with no song catalog, drops the request", () => {
  const p = smsProject({});
  p.project.requestSong({ name: "NOSUCH", confirmed: false }); // renamed / deleted since the row was recorded
  expect(p.project.settleSong()).toBe("dropped");
  expect(p.project.hasSongRequest()).toBe(false);
  p.project.requestSong({ index: 7, confirmed: true });
  expect(p.project.settleSong()).toBe("dropped");
  expect(p.writes()).toBe(0);

  // A plain battery cart has no catalog: nothing could ever be loaded into it by name.
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  be.seed("/roms/game.gb", gbRomBattery());
  project.systems.addSystem("/roms/game.gb");
  project.requestSong({ name: "ANY", confirmed: false });
  expect(project.settleSong()).toBe("dropped");

  // And with no cart at all, or nothing requested, there is nothing to settle.
  const empty = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  expect(empty.settleSong()).toBe("idle");
  empty.requestSong({ name: "ANY", confirmed: false });
  expect(empty.settleSong()).toBe("dropped");
});

test("a cart that never boots spends the budget, and the request is dropped - never applied late", () => {
  // A request that waits forever is a write that lands in some later, unrelated session of the same
  // tile. ~15 s of ticks is five times an smsggdj boot.
  const p = smsProject({ booted: false });
  p.project.requestSong({ name: "ALPHA", confirmed: false });
  let waited = 0;
  let last: SongSettle = "idle";
  for (let i = 0; i < 2000 && (last = p.project.settleSong()) === "waiting"; i++) waited++;
  expect(last).toBe("dropped");
  expect(waited).toBe(900);
  expect(p.writes()).toBe(0);
  p.boot(); // the cart comes up later: nothing is waiting for it
  expect(p.project.settleSong()).toBe("idle");
  expect(p.working()).toEqual(new Uint8Array(SMDJ4_BLOCK_LEN));
});

test("a new request replaces a parked one, and a new or reloaded project clears it", () => {
  const p = smsProject({ booted: false });
  p.project.requestSong({ name: "ALPHA", confirmed: false });
  p.project.requestSong({ name: "BETA", confirmed: false }); // changed their mind
  p.boot();
  expect(p.project.settleSong()).toBe("loaded");
  expect(p.working()).toEqual(block(2));

  // Parked against this cart, then the project is replaced: the request cannot mean anything to the
  // next cart, so it is gone rather than applied to it.
  p.boot({ booted: false });
  p.project.requestSong({ name: "ALPHA", confirmed: false });
  expect(p.project.settleSong()).toBe("waiting");
  p.project.newProject();
  expect(p.project.hasSongRequest()).toBe(false);
  expect(p.project.settleSong()).toBe("idle");

  const q = smsProject({ booted: false });
  q.project.requestSong({ name: "ALPHA", confirmed: false });
  expect(q.project.load("/proj/sms.rplg").kind).toBe("loaded");
  expect(q.project.hasSongRequest()).toBe(false);
});

test("LSDj settles synchronously - its working song is the battery, ready from the first frame", () => {
  // The other consoles must keep their immediacy: a Songs-menu Load on LSDj cold-boots right there, and a
  // Recent row's song is loaded before the menu has even closed. Nothing waits when nothing has to.
  const SONG = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
  const sav = loadSongToWorking(
    savFrom({ activeProjectIndex: 0, projects: [{ name: "GRUB", version: 0, song: SONG }, { name: "INTRO", version: 0, song: SONG }] } as SavInput),
    0,
  )!;
  const be = new MockBackend("/cfg");
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, buildAppRegistry());
  be.seed("/roms/lsdj.gb", lsdjRom("LSDJ-V9.4.2"));
  const id = project.systems.addSystem("/roms/lsdj.gb")!;
  be.setSram(id, sav);
  expect(project.save("/proj/lsdj.rplg")).toBeTruthy();
  expect(recent.view().map((v) => v.song)).toEqual(["GRUB"]); // recorded at once: no readiness to wait for

  const builds = be.constructCalls.length;
  project.requestSong({ name: "INTRO", confirmed: false });
  expect(project.settleSong()).toBe("loaded"); // this very call
  expect(be.constructCalls.length).toBe(builds + 1); // the cold-boot spine: one rebuild from the written .sav
  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO");
  expect(recent.view().map((v) => v.song)).toEqual(["INTRO", "GRUB"]);
});
