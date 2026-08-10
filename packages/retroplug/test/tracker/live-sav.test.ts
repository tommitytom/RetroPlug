// Editing a running cart's battery: mutateLiveSav (the shape every Songs-menu edit takes) and
// loadSongByName (what a recent song row runs once its project has loaded). Driven over the REAL
// SystemsStore + role registry against the MockBackend, so the role-derived song catalog, the
// resolved .sav target and the cold-boot rebuild are all exercised, not stubbed.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { buildAppRegistry } from "../../src/appHost";
import { mutateLiveSav, loadSongByName, loadSongInPrimary, lsdjSongCatalog, songLoadWouldDiscard, songLoadByNameWouldDiscard, savEditWouldDiscard } from "../../src/tracker";
import { buildSav, SMDJ4_BLOCK_LEN } from "../../src/smsggdj/codec/sav";
import { lsdjRom, gbRomBattery } from "../systems/fixtures";
import { savFrom, loadSongToWorking, type SavInput } from "../../src/lsdjSav";

const SONG = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };

// An LSDj battery with GRUB + INTRO saved and `active` genuinely loaded into working memory. savFrom alone
// only sets the active POINTER, leaving working memory as the model's default - which reads as uncommitted
// work (correctly: it matches no slot). Copying the song in is what a real cart looks like after a load.
function lsdjSav(active: number): Uint8Array {
  const sav = savFrom({
    activeProjectIndex: active,
    projects: [
      { name: "GRUB", version: 0, song: SONG },
      { name: "INTRO", version: 0, song: SONG },
    ],
  } as SavInput);
  return loadSongToWorking(sav, active) ?? sav;
}

// A live LSDj system with GRUB working. The store carries the role registry, so the cart resolves a song
// catalog exactly as it does in the app.
function newCart(rom: Uint8Array = lsdjRom("LSDJ-V9.4.2")) {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/lsdj.gb", rom);
  const id = systems.addSystem("/roms/lsdj.gb")!;
  be.setSram(id, lsdjSav(0));
  return { be, systems, sys: () => systems.systems()[0] };
}

test("mutateLiveSav: writes the resolved .sav and cold-boots the cart from it", () => {
  const { be, systems, sys } = newCart();
  const before = sys().id;

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();

  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO"); // durable on disk
  expect(sys().id).toBe(before + 1); // rebuilt in place (a new core id), so the running cart followed
});

test("mutateLiveSav: a declining transform leaves the cart and its .sav untouched", () => {
  const { be, systems, sys } = newCart();
  const before = sys().id;
  expect(mutateLiveSav(be, systems, sys(), () => null)).toBeFalsy();
  expect(be.readFile("/roms/lsdj.sav")).toBe(null); // nothing written
  expect(sys().id).toBe(before); // nothing rebuilt
});

test("loadSongByName: loads that song into working memory, addressing it by NAME not slot", () => {
  const { be, systems, sys } = newCart();
  expect(loadSongByName(be, systems, sys(), "INTRO")).toBeTruthy();
  // The cart is booted from the written battery, so that file IS the loaded state (the mock backend has no
  // core to read a fresh snapshot back from).
  const written = be.readFile("/roms/lsdj.sav")!;
  expect(lsdjSongCatalog.workingName(written)).toBe("INTRO");
  expect(lsdjSongCatalog.list(written).map((s) => s.name)).toEqual(["GRUB", "INTRO"]); // both songs still saved
});

test("loadSongByName: the song already loaded is a no-op (no rebuild, no write)", () => {
  const { be, systems, sys } = newCart();
  const before = sys().id;
  expect(loadSongByName(be, systems, sys(), "GRUB")).toBeFalsy(); // GRUB is already working
  expect(sys().id).toBe(before);
  expect(be.readFile("/roms/lsdj.sav")).toBe(null);
});

test("loadSongByName: an unknown song / a non-tracker cart declines instead of touching the battery", () => {
  const { be, systems, sys } = newCart();
  expect(loadSongByName(be, systems, sys(), "GONE")).toBeFalsy(); // deleted or renamed since it was recorded
  expect(loadSongByName(be, systems, sys(), "")).toBeFalsy();
  expect(be.readFile("/roms/lsdj.sav")).toBe(null);

  const plain = newCart(gbRomBattery()); // a battery cart with no song catalog
  expect(loadSongByName(plain.be, plain.systems, plain.sys(), "GRUB")).toBeFalsy();
});

test("loadSongInPrimary: targets the focused system, and declines with no systems at all", () => {
  const { be, systems } = newCart();
  const second = systems.addSystem("/roms/lsdj.gb")!; // a second instance, taking /roms/lsdj-2.sav
  be.setSram(second, lsdjSav(0));
  systems.setFocus(second);

  expect(loadSongInPrimary(be, systems, "INTRO")).toBeTruthy();
  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj-2.sav")!)).toBe("INTRO"); // the FOCUSED one
  expect(be.readFile("/roms/lsdj.sav")).toBe(null); // the unfocused instance was left alone

  const empty = new SystemsStore(new MockBackend("/cfg"), () => {}, buildAppRegistry());
  expect(loadSongInPrimary(be, empty, "INTRO")).toBeFalsy();
});

// --- the rolling backup -----------------------------------------------------------------------------
// Every destructive battery edit goes through mutateLiveSav, so backing up there covers Load / Replace /
// Delete / Add / reorder at once - including any op added later that forgets to think about it. It is the
// last line of defence when a confirm is dismissed, or a path grows that never raises one.

test("mutateLiveSav: writes a <sav>.bak of the PRE-EDIT battery before overwriting the .sav", () => {
  const { be, systems, sys } = newCart();
  const before = be.readSram(sys().id)!; // GRUB working

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();

  // The .sav is the NEW state...
  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO");
  // ...and the backup is exactly what was there before, so the discarded working song is recoverable.
  const bak = be.readFile("/roms/lsdj.sav.bak");
  expect(bak != null).toBeTruthy();
  expect([...bak!]).toEqual([...before]);
  expect(lsdjSongCatalog.workingName(bak!)).toBe("GRUB");
});

test("mutateLiveSav: the backup is the LIVE battery, not the stale copy on disk", () => {
  const { be, systems, sys } = newCart();
  // Simulate the OnProjectSave default: an older mirror on disk while the live battery has moved on.
  be.writeFile("/roms/lsdj.sav", lsdjSav(1));
  const live = be.readSram(sys().id)!; // still GRUB working

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();

  // Backing up the file would have preserved the stale INTRO state and lost the live one.
  expect([...be.readFile("/roms/lsdj.sav.bak")!]).toEqual([...live]);
});

test("mutateLiveSav: a declining transform writes no backup either", () => {
  const { be, systems, sys } = newCart();
  expect(mutateLiveSav(be, systems, sys(), () => null)).toBeFalsy();
  expect(be.readFile("/roms/lsdj.sav.bak")).toBe(null);
});

test("mutateLiveSav: a backup that CANNOT be written never blocks the edit", () => {
  const { be, systems, sys } = newCart();
  // The RPC layer throws on a backend error (makeCall turns an error reply into an exception), so this is
  // what a read-only ROM folder looks like from here. The safety net must not become the failure.
  const realWriteFile = be.writeFile.bind(be);
  be.writeFile = (path: string, data: Uint8Array) => {
    if (path.endsWith(".bak")) throw new Error("EROFS: read-only file system");
    return realWriteFile(path, data);
  };

  expect(mutateLiveSav(be, systems, sys(), (sav) => lsdjSongCatalog.load(sav, 1))).toBeTruthy();
  expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO"); // the edit still landed
  expect(be.readFile("/roms/lsdj.sav.bak")).toBe(null); // just without a backup
});

// --- the guard's decision, shared by the Songs menu and the Recent list ------------------------------
// Both destroy the working song through the same catalog.load, so both ask the same question here rather
// than each deciding for itself.

test("songLoadWouldDiscard: true only when the working song is committed nowhere", () => {
  const { be, systems, sys } = newCart();
  expect(songLoadWouldDiscard(systems, sys())).toBe(false); // GRUB working == its slot

  const edited = be.readSram(sys().id)!.slice();
  edited[0x100] ^= 0xff;
  be.setSram(sys().id, edited);
  expect(songLoadWouldDiscard(systems, sys())).toBe(true);
});

test("songLoadWouldDiscard: a non-tracker cart never prompts (no positive signal, no warning)", () => {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  be.seed("/roms/plain.gb", gbRomBattery());
  const id = systems.addSystem("/roms/plain.gb")!;
  be.setSram(id, new Uint8Array(0x2000).fill(7));
  expect(songLoadWouldDiscard(systems, systems.systems()[0])).toBe(false);
});

// --- the OTHER five ops, on a console whose working song is not in the battery ----------------------

/** A live smsggdj cart: a .sms carrying the build marker, with a two-song SMDJ4 battery and a synthetic
 *  work-RAM block - the region the cart actually composes in, and which no `.sav` ever contains. */
function newSmsCart() {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  const rom = new Uint8Array(0x8200);
  rom.set([0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41], 0x7ff0); // "TMR SEGA"
  rom[0x7ff0 + 0xf] = 0x40; // region nibble 4 -> SMS
  for (let i = 0; i < "SMSGGDJ".length; i++) rom[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  be.seed("/roms/smsggdj.sms", rom);
  const id = systems.addSystem("/roms/smsggdj.sms")!;
  const block = (tag: number): Uint8Array => {
    const b = new Uint8Array(SMDJ4_BLOCK_LEN);
    for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
    return b;
  };
  be.setSram(id, buildSav([{ block: block(1), name: "ALPHA" }, { block: block(2), name: "BETA" }], 32 * 1024)!);
  const setWorking = (b: Uint8Array): void => {
    const ram = new Uint8Array(8192);
    ram.set(b, 0);
    be.setRam(id, ram);
  };
  return { be, systems, sys: () => systems.systems()[0], block, setWorking };
}

test("savEditWouldDiscard: a battery edit warns on the console whose working song is NOT in the battery", () => {
  // The gap the review found. mutateLiveSav always ends in a cold boot; for LSDj and risa that is free
  // because the working song is inside the bytes being rewritten, so the shared menu guarded Load alone.
  // Here the working song is work RAM and the reboot destroys it, so Delete / Move / Add / Import /
  // Replace are every bit as destructive as Load and have to ask first.
  const { systems, sys, block, setWorking } = newSmsCart();

  setWorking(block(2)); // working song IS the saved BETA - the reboot costs nothing
  expect(savEditWouldDiscard(systems, sys())).toBe(false);

  const edited = block(2);
  edited[9] ^= 0xff; // ...now edited, and in no slot
  setWorking(edited);
  expect(savEditWouldDiscard(systems, sys())).toBe(true);
  expect(songLoadWouldDiscard(systems, sys())).toBe(true); // Load agrees, off the same signal
});

test("savEditWouldDiscard: with no work RAM published it stays silent rather than guessing", () => {
  // The mock publishes no WRAM until a test sets one, which is exactly the "cannot tell" case. A prompt
  // fired on no evidence is the one that teaches people to dismiss prompts.
  const { systems, sys } = newSmsCart();
  expect(savEditWouldDiscard(systems, sys())).toBe(false);
});

test("savEditWouldDiscard: LSDj is never warned, because its reboot restores the working song", () => {
  // The reason this is a per-console flag and not a blanket confirm. GRUB is dirty here - the LOAD guard
  // fires - yet a Delete rewrites the .sav with working memory intact and the cold boot brings it back,
  // so warning about it would be false.
  const { be, systems, sys } = newCart();
  const edited = be.readSram(sys().id)!.slice();
  edited[0x100] ^= 0xff;
  be.setSram(sys().id, edited);
  expect(songLoadWouldDiscard(systems, sys())).toBe(true);
  expect(savEditWouldDiscard(systems, sys())).toBe(false);
});

test("songLoadByNameWouldDiscard: re-picking the song you are ON never prompts", () => {
  const { be, systems, sys } = newCart();
  const edited = be.readSram(sys().id)!.slice();
  edited[0x100] ^= 0xff; // dirty, so the plain guard WOULD fire
  be.setSram(sys().id, edited);
  expect(songLoadWouldDiscard(systems, sys())).toBe(true);

  // ...but loadSongByName no-ops for the loaded song, so it destroys nothing and must stay silent.
  expect(songLoadByNameWouldDiscard(systems, sys(), "GRUB")).toBe(false);
  // A DIFFERENT song would really load, so the warning stands.
  expect(songLoadByNameWouldDiscard(systems, sys(), "INTRO")).toBe(true);
  // A song that isn't there loads nothing either.
  expect(songLoadByNameWouldDiscard(systems, sys(), "NOSUCH")).toBe(false);
});
