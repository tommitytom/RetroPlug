// Noticing that the player edited the song ON THE CART.
//
// The controller's row-timing table is derived on the control plane and pushed to the audio thread as
// data, so it only moves when something re-pushes the structure - and nothing did. Adding a chain to a
// song row inside LSDj left the Launchpad's grid showing the song as it was; toggling "Use in Project"
// off and on was the only way through, because that drops the stage and takes its state with it.
// Reported from a hardware session.
//
// A cart being edited on its own screen emits no signal at all, so this is a poll. It has to be cheap
// enough to sit on the same 0.5 s timer as the recents song-watch, and it has to be quiet: a re-push on
// every instrument tweak would re-derive the table (a whole sav decode) for a change that cannot affect
// it. Hence a signature over only the four regions row timing actually depends on.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { workingSongSignature } from "../../src/lsdj/playback/fromSav";
import * as R from "../../src/lsdj/codec/regions";
import { gbRom } from "../systems/fixtures";
import { identifyLsdj, resolveLayout } from "../../src/lsdj/runtime";

/** A blank song bank, big enough to be a plausible battery. */
function sav(): Uint8Array {
  return new Uint8Array(R.kSongByteCount + 0x200);
}

/** A full-size battery that passes isLsdjSav ('jk' at 0x813e), with `sync` in the working song's setting
 *  byte - enough for the byte-level reads without building a whole song. */
function lsdjSav(sync: number): Uint8Array {
  const b = new Uint8Array(0x20000);
  b[0x813e] = 0x6a; // 'j'
  b[0x813f] = 0x6b; // 'k'
  b[R.kModernRegions.syncMode] = sync;
  return b;
}

test("the signature moves when a song row's chain changes", () => {
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.chainAssignments + 4 * R.kChannelCount] = 3; // row 4, pu1 -> chain 3
  expect(workingSongSignature(a) !== before).toBe(true);
});

test("the signature moves when a chain gains or loses a phrase", () => {
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.chainPhrases + 3 * R.kChainLength + 1] = 9; // chain 3, slot 1 -> phrase 9
  expect(workingSongSignature(a) !== before).toBe(true);
  const withPhrase = workingSongSignature(a);
  a[R.kModernRegions.chainAllocations] ^= 0x08; // chain 3 allocated/not
  expect(workingSongSignature(a) !== withPhrase).toBe(true);
});

test("the signature moves when groove 0 changes, because that sets the row duration", () => {
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.grooves + 1] = 3;
  expect(workingSongSignature(a) !== before).toBe(true);
});

test("the signature IGNORES edits that cannot change row timing", () => {
  // Renaming a file, tweaking an instrument, writing a phrase note: all real edits, none of which move a
  // single row's duration. Re-pushing for these would decode the whole sav for nothing, several times a
  // minute, while somebody works.
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.instrumentNames + 2] = 0x41;
  a[R.kModernRegions.instrumentParams + 5] = 0x7f;
  a[R.kModernRegions.phraseNotes + 40] = 0x50;
  a[R.kModernRegions.tempo] = 150;
  expect(workingSongSignature(a)).toBe(before);
});

test("an unreadable battery is inert rather than a change", () => {
  // Null / too short / not yet published. Returning a constant means the poll simply never fires, which
  // is what "no song feedback" should look like everywhere else in this feature.
  expect(workingSongSignature(null)).toBe(0);
  expect(workingSongSignature(new Uint8Array(16))).toBe(0);
  expect(workingSongSignature(undefined)).toBe(0);
});

test("refreshControllerSong re-pushes the kernel exactly once per real edit", () => {
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, sav());
  stores.project.setController({ enabled: true });

  let pushes = 0;
  stores.project.setOnSystemsChange(() => void pushes++);

  expect(stores.project.refreshControllerSong()).toBe(true); // first look: adopt whatever is there
  expect(pushes).toBe(1);
  expect(stores.project.refreshControllerSong()).toBe(false); // unchanged -> silent
  expect(pushes).toBe(1);

  const edited = sav();
  edited[R.kModernRegions.chainAssignments + 4 * R.kChannelCount] = 3;
  be.setSram(id, edited);
  expect(stores.project.refreshControllerSong()).toBe(true);
  expect(pushes).toBe(2);
  expect(stores.project.refreshControllerSong()).toBe(false);
  expect(pushes).toBe(2);
});

test("refreshControllerSong costs nothing while no controller is enabled", () => {
  // It runs on a 60 fps hook's timer in every session, including the overwhelming majority with no
  // Launchpad anywhere near them, so it must not even read the battery.
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const reads = be.readSramCalls.length;
  expect(stores.project.refreshControllerSong()).toBe(false);
  expect(be.readSramCalls.length).toBe(reads);
});

test("an edit does not dirty the project - the song lives in the cart, not the .rplg", () => {
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, sav());
  stores.project.setController({ enabled: true });
  stores.project.refreshControllerSong();

  // markDirty is what fires onChange, so counting that is the precise test: a re-push must re-drive the
  // KERNEL without touching the project's saved state. Otherwise every edit in LSDj would put an unsaved
  // asterisk on a project file whose contents did not move.
  let changes = 0;
  stores.project.setOnChange(() => void changes++);
  const edited = sav();
  edited[R.kModernRegions.chainAssignments] = 7;
  be.setSram(id, edited);
  expect(stores.project.refreshControllerSong()).toBe(true);
  expect(changes).toBe(0);
});

test("the poll also records the cart's SYNC mode, which is how the menu can warn about it", () => {
  // A cart that is not in MI.MAP ignores launches silently - it keeps playing and stepping, so the only
  // symptom is that the pads stop working. Detecting it is the difference between "the Launchpad is
  // broken" and "your cart is in MI.OUT".
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const id = stores.project.systems.view()[0].id;
  stores.project.setController({ enabled: true });

  const inMap = lsdjSav(8); // MidiMap
  be.setSram(id, inMap);
  stores.project.refreshControllerSong();
  expect(stores.project.controllerCartSync()).toBe("MidiMap");

  be.setSram(id, lsdjSav(9)); // the player knocked it to MI.OUT
  stores.project.refreshControllerSong();
  expect(stores.project.controllerCartSync()).toBe("MidiOut");
});

test("a non-LSDj battery reports no sync mode rather than a wrong one", () => {
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", gbRom());
  stores.project.systems.addSystem("/roms/a.gb");
  const id = stores.project.systems.view()[0].id;
  stores.project.setController({ enabled: true });
  be.setSram(id, sav()); // all zeroes: no LSDj header
  stores.project.refreshControllerSong();
  expect(stores.project.controllerCartSync()).toBe(null);
});

// --- the start-edge anchor -------------------------------------------------------------------------
//
// Pressing START on LSDj's own song screen starts the cart wherever ITS cursor is. Nothing tells the app,
// so the predictor carried on from where it thought it was and the lit playhead pointed at the wrong row
// until the next pad press. The control plane can see it, on the not-playing -> playing edge.

/** A GB ROM whose title identifies it as the aboy build, so the WRAM reader resolves a layout. */
function lsdjRom(): Uint8Array {
  const b = gbRom();
  const title = "LSDJ-V9.3.3ABOY";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

/** A WRAM image in which `rows[ch]` is playing (null = silent), laid out with the same offset table the
 *  reader resolves - so this forges what a real cart would show, not what we wish it showed. */
function wramWith(rows: (number | null)[]): Uint8Array {
  const layout = resolveLayout(identifyLsdj(lsdjRom()));
  const w = new Uint8Array(0x2000);
  if (!layout) return w;
  for (let ch = 0; ch < 4; ch++) {
    w[layout.active + ch] = rows[ch] === null ? 0 : 1;
    w[layout.songRows + ch] = rows[ch] ?? 0;
  }
  return w;
}

function controllerCart(): { stores: ReturnType<typeof composeAppStores>; be: MockBackend; id: number } {
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/lsdj.gb", lsdjRom());
  stores.project.systems.addSystem("/roms/lsdj.gb");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, sav());
  stores.project.setController({ enabled: true });
  return { stores, be, id };
}

test("the cart starting on its own is captured as an anchor, once per start", () => {
  const { stores, be, id } = controllerCart();
  expect(stores.project.controllerStartAnchor()).toBe(null);

  be.setRam(id, wramWith([null, null, null, null])); // idle
  stores.project.refreshControllerSong();
  expect(stores.project.controllerStartAnchor()).toBe(null);

  be.setRam(id, wramWith([5, 5, 5, 5])); // the player pressed START with the cursor on row 5
  stores.project.refreshControllerSong();
  const first = stores.project.controllerStartAnchor();
  expect(first?.rows).toEqual([5, 5, 5, 5]);
  expect(first?.seq).toBe(1);

  // Still playing, now further along. NOT a new anchor: this is the edge, not a position feed - correcting
  // mid-song every half second is a different feature (M6) and would fight the predictor's own clock.
  be.setRam(id, wramWith([6, 6, 6, 6]));
  stores.project.refreshControllerSong();
  expect(stores.project.controllerStartAnchor()?.seq).toBe(1);

  // Stop, then start again: a new anchor, with a new sequence so the role applies it.
  be.setRam(id, wramWith([null, null, null, null]));
  stores.project.refreshControllerSong();
  be.setRam(id, wramWith([2, 2, 2, 2]));
  stores.project.refreshControllerSong();
  const second = stores.project.controllerStartAnchor();
  expect(second?.rows).toEqual([2, 2, 2, 2]);
  expect(second?.seq).toBe(2);
});

test("the anchor is per channel, because channels diverge in normal play", () => {
  const { stores, be, id } = controllerCart();
  be.setRam(id, wramWith([null, null, null, null]));
  stores.project.refreshControllerSong();
  be.setRam(id, wramWith([4, 3, 4, null])); // pu2 lagging on a long chain, noi silent
  stores.project.refreshControllerSong();
  expect(stores.project.controllerStartAnchor()?.rows).toEqual([4, 3, 4, null]);
});

test("a start edge re-pushes the kernel, or the anchor would sit on the control plane doing nothing", () => {
  const { stores, be, id } = controllerCart();
  be.setRam(id, wramWith([null, null, null, null]));
  stores.project.refreshControllerSong();
  let pushes = 0;
  stores.project.setOnSystemsChange(() => void pushes++);
  be.setRam(id, wramWith([1, 1, 1, 1]));
  expect(stores.project.refreshControllerSong()).toBe(true);
  expect(pushes).toBe(1);
});
