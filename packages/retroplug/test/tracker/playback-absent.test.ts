// A system whose tracker integration has no `playback` member reads "no sync mode, no signature, no timing table, no
// reader" (docs/modular-graph-phase-f4a-design.md section 6, edge E3). For a battery that is not the cart's, that is
// what LSDj's own functions answered before the controller poll went through the integration - which is what the poll
// relied on, since it asked them of every system. The exceptions are recorded here too, because they are the reason
// this is a narrowing rather than a no-op: no shipped combination reaches them (MI.MAP drives an LSDj cart), and they
// are the two cases where a non-LSDj battery used to move the controller's state.
import { test, expect } from "@retroplug/testing";
import { lsdjIntegration } from "@retroplug/lsdj/lsdjIntegration";
import { kModernRegions, kSongByteCount } from "@retroplug/lsdj/codec/regions";
import { savBytes as risaSav } from "@retroplug/risa/testFixtures";
import type { TrackerPlayback } from "@retroplug/tracker/trackerPlayback";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { gbRom } from "../systems/fixtures";

const LSDJ = lsdjIntegration.playback!;

/** The answers a system with no playback member gives: no sync mode, no signature, no table, no reader. */
const ABSENT: TrackerPlayback = {
  syncMode: () => null,
  songSignature: () => 0,
  rowTicks: () => null,
  anchor: { reader: () => null, playing: () => false, rows: () => [] },
};

/** A 64 KiB battery no cart's (risa's v2 catalog): past LSDj's song bank, short of a full LSDj image. */
const otherCart = risaSav("v2_blumarbl");
/** A full-size image that carries no LSDj 'jk' magic: the size `workingSongSignature` hashes, and `decodeSav` refuses. */
const unrecognised = new Uint8Array(0x20000);

/** The project section the Launchpad pack contributes, and what its service published (as the menus read it). */
const CONTROLLER_SECTION = "controller";
const cartSyncOf = (stores: ReturnType<typeof composeAppStores>): string | null =>
  stores.project.serviceState<{ cartSync: string | null }>(CONTROLLER_SECTION)?.cartSync ?? null;

test("E3: the SYNC setting agrees - neither answers for a battery that is not LSDj's", () => {
  for (const sram of [otherCart, unrecognised, null]) {
    expect(LSDJ.syncMode(sram)).toBe(ABSENT.syncMode(sram));
    expect(LSDJ.syncMode(sram)).toBe(null);
  }
});

test("E3: the timing table agrees for an image no cart can decode", () => {
  // decodeSav throws on a full-size image with no magic, and songRowTicksFromSav answers null rather than throwing:
  // the same null an absent member gives, so the controller's empty table is unchanged.
  expect(LSDJ.rowTicks(unrecognised)).toBe(null);
  expect(ABSENT.rowTicks(unrecognised)).toBe(null);
});

test("E3: the timing table DIFFERS for a battery between 32 and 128 KiB (nothing shipped reaches it)", () => {
  // decodeSav reads a 64 KiB image as an early-LSDj one and songRowTicks runs over bytes that are not a song, so the
  // cart answers a table where an absent member says none. The controller then shaded rows of a battery that is not
  // an LSDj cart's; MI.MAP never drives one, so no shipped project ever saw it.
  expect(otherCart.length >= kSongByteCount && otherCart.length < 0x20000).toBe(true);
  expect(LSDJ.rowTicks(otherCart) !== null).toBe(true);
  expect(ABSENT.rowTicks(otherCart)).toBe(null);
});

test("E3: the song signature DIFFERS for any image of at least one song bank (nothing shipped reaches it)", () => {
  // workingSongSignature hashes the song-structure regions of ANY image that long, so a non-LSDj battery used to
  // report a moving signature - re-publishing the graph and notifying the systems observers on the poll. An absent
  // member answers 0, which compares equal to itself and so never fires.
  expect(otherCart.length >= kSongByteCount).toBe(true);
  const sig = LSDJ.songSignature(otherCart);
  expect(sig !== 0).toBe(true);
  expect(sig === ABSENT.songSignature(otherCart)).toBe(false);
  // It MOVES: a byte in a region the table is derived from is what an in-cart edit looks like.
  const edited = otherCart.slice();
  edited[kModernRegions.chainAssignments] ^= 0xff;
  expect(LSDJ.songSignature(edited) !== sig).toBe(true);
});

test("E3: the reader agrees - an absent member and an unsupported build both have none", () => {
  const notLsdj = new Uint8Array(0x150); // a Game Boy header whose title is not LSDj's
  expect(LSDJ.anchor.reader(notLsdj)).toBe(null);
  expect(ABSENT.anchor.reader(notLsdj)).toBe(null);
  // Neither ever reports a start, so the not-playing -> playing edge cannot fire.
  expect(ABSENT.anchor.playing(null)).toBe(false);
  expect(ABSENT.anchor.rows(null)).toEqual([]);
});

// ---- the store's absent path, which is what E3 actually changes -----------------------------------

test("E3: a system that runs no tracker neither publishes nor notifies, and holds no timing table", () => {
  // The two DIFFERS cases above are what the old poll acted on: a non-LSDj battery moved the signature, so the poll
  // re-published the graph and notified every observer, and its timing table shaded rows. A system whose integration
  // has no playback now answers all four reads itself, so none of that fires - the store-level half of E3.
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/other.gb", gbRom()); // a Game Boy cart whose title names no tracker
  stores.project.systems.addSystem("/roms/other.gb");
  const id = stores.project.systems.view()[0].id;
  be.setSram(id, otherCart.slice()); // a 64 KiB battery: the one the OLD poll read a moving signature from
  stores.project.setSection(CONTROLLER_SECTION, { enabled: true });

  let publishes = 0;
  const graph = stores.project.graph;
  const publish = graph.publish.bind(graph);
  graph.publish = (...args) => {
    publishes++;
    return publish(...args);
  };
  let notifies = 0;
  stores.project.addOnSystemsChange(() => void notifies++);

  // The first look adopts nothing either: with no playback there is no signature to adopt and no start to anchor.
  expect(stores.project.pollServices()).toBe(false);
  expect(publishes).toBe(0);
  expect(notifies).toBe(0);
  expect(cartSyncOf(stores)).toBe(null);
  // The model the publish derives: an empty table reads as "no content anywhere", which is what a table-less
  // integration gives.
  expect(be.graphNode("controller")?.settings?.songRowTicks).toEqual([]);

  // An edit in the region the signature hashed: the battery moves, the poll stays silent.
  const edited = otherCart.slice();
  edited[kModernRegions.chainAssignments] ^= 0xff;
  be.setSram(id, edited);
  expect(stores.project.pollServices()).toBe(false);
  expect(publishes).toBe(0);
  expect(notifies).toBe(0);
});
