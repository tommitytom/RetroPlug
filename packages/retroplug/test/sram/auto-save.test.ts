// The SRAM auto-save (mirror) write policy: the pure dedup/seed/write decision, and the
// SramAutoSaver over a MockBackend + real SystemsStore + UserConfigStore. Proves the
// mode gating (Off/OnProjectSave/Continuous), dirty-hash dedup, seed-vs-write on first
// observation, the override write target, and skipping embedded systems.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { UserConfigStore } from "../../src/userConfigStore";
import { SramAutoSaver, hashBytes, decideAutoSave, sramDirtyCount, dirtySramTargets, flushDirtySram, lsdjSramSignature, sramSignature } from "../../src/sramAutoSave";
import { savFrom } from "../../src/lsdj";
import { gbRom } from "../systems/fixtures";

const bytes = (...b: number[]) => new Uint8Array(b);
const SAV = "/proj/a.sav";

// --- pure kernel ---

test("hashBytes: stable and content-sensitive", () => {
  expect(hashBytes(bytes(1, 2, 3))).toBe(hashBytes(bytes(1, 2, 3)));
  expect(hashBytes(bytes(1, 2, 3)) === hashBytes(bytes(1, 2, 4))).toBeFalsy();
});

test("decideAutoSave: dedup / seed / write", () => {
  const sav = bytes(1, 2, 3);
  const h = hashBytes(sav);
  expect(decideAutoSave(sav, h, null)).toEqual({ write: false, hash: h }); // unchanged
  expect(decideAutoSave(sav, null, bytes(1, 2, 3))).toEqual({ write: false, hash: h }); // first-obs, matching file → seed
  expect(decideAutoSave(sav, null, null)).toEqual({ write: true, hash: h }); // first-obs, no file → write
  expect(decideAutoSave(sav, null, bytes(9)).write).toBeTruthy(); // first-obs, different file → write
  expect(decideAutoSave(sav, h + 1, null).write).toBeTruthy(); // changed → write
});

// --- store ---

function setup() {
  const be = new MockBackend("/config");
  const uc = new UserConfigStore(be); // default sramAutoSave = "OnProjectSave"
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, uc);
  be.seed("/proj/a.gb", gbRom());
  const id = systems.addSystem("/proj/a.gb")!; // savPath resolves to /proj/a.sav
  return { be, uc, systems, saver, id };
}

test("flushOnSave (OnProjectSave): writes the resolved <rom>.sav, then dedups", () => {
  const { be, saver, id } = setup();
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.flushOnSave()).toBe(1);
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
  expect(saver.flushOnSave()).toBe(0); // on-disk matches live → seed, no rewrite
});

test("flushOnSave: a changed SRAM is rewritten", () => {
  const { be, saver, id } = setup();
  be.setSram(id, bytes(1, 2, 3));
  saver.flushOnSave();
  be.setSram(id, bytes(4, 5, 6));
  expect(saver.flushOnSave()).toBe(1);
  expect([...be.readFile(SAV)!]).toEqual([4, 5, 6]);
});

test("Off: flushOnSave and pump are no-ops", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Off");
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.flushOnSave()).toBe(0);
  expect(saver.pump()).toBe(0);
  expect(be.readFile(SAV)).toBe(null); // nothing written
});

test("pump: writes only in Continuous, and only on change", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.pump()).toBe(1); // no file yet → write
  expect([...be.readFile(SAV)!]).toEqual([1, 2, 3]);
  expect(saver.pump()).toBe(0); // unchanged (persistent hash) → no write
  be.setSram(id, bytes(7));
  expect(saver.pump()).toBe(1); // changed → write
  expect([...be.readFile(SAV)!]).toEqual([7]);
});

test("pump: seeds (no write) when an identical .sav is already on disk", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.setSram(id, bytes(5, 5));
  be.seed(SAV, bytes(5, 5)); // a just-loaded, identical sibling
  expect(saver.pump()).toBe(0); // first-obs matches → seed, no rewrite
});

test("embedded system (no romPath) is skipped", () => {
  const be = new MockBackend("/config");
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, new UserConfigStore(be));
  systems.loadMgb(); // embedded, no romPath / sibling
  expect(saver.flushOnSave()).toBe(0);
  expect(be.log.includes("writeFile")).toBeFalsy();
});

test("a paired savPath override is honored as the write target", () => {
  const be = new MockBackend("/config");
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, new UserConfigStore(be));
  be.seed("/proj/a.gb", gbRom());
  be.seed("/saves/custom.sav", bytes(0)); // a different paired save → becomes the override
  const id = systems.addSystem("/proj/a.gb", { explicitSav: "/saves/custom.sav" })!;
  be.setSram(id, bytes(1, 1));
  expect(saver.flushOnSave()).toBe(1);
  expect([...be.readFile("/saves/custom.sav")!]).toEqual([1, 1]); // wrote to the override, not /proj/a.sav
  expect(be.readFile(SAV)).toBe(null);
});

// --- unsaved-SRAM check (the close-confirm signal; whole-SRAM signature) ---

test("sramDirtyCount: dirty when the live battery has no matching .sav; clean once mirrored", () => {
  const { be, systems, id } = setup();
  const list = systems.systems();
  expect(sramDirtyCount(be, list)).toBe(1); // fresh system has a live battery but no .sav on disk → dirty
  be.seed(SAV, be.readSram(id)!); // an identical sibling is now on disk
  expect(sramDirtyCount(be, list)).toBe(0); // matches → clean
  be.setSram(id, bytes(9, 9)); // an in-game battery write
  expect(sramDirtyCount(be, list)).toBe(1); // differs from disk → dirty again
});

test("dirtySramTargets: names the .sav each dirty battery writes, flagging one that isn't on disk yet", () => {
  const { be, systems, id } = setup();
  const list = systems.systems();
  // Fresh system: a live battery with no .sav beside it - a save would CREATE the file.
  expect(dirtySramTargets(be, list)).toEqual([{ id, savPath: SAV, isNew: true }]);

  be.seed(SAV, be.readSram(id)!); // mirrored
  expect(dirtySramTargets(be, list)).toEqual([]);

  be.setSram(id, bytes(9, 9)); // an in-game battery write: the file exists but differs
  expect(dirtySramTargets(be, list)).toEqual([{ id, savPath: SAV, isNew: false }]);
  expect(sramDirtyCount(be, list)).toBe(dirtySramTargets(be, list).length); // the count IS the list length
});

test("flushDirtySram: writes each dirty battery to its sibling .sav, then it's clean", () => {
  const { be, systems, id } = setup();
  be.setSram(id, bytes(4, 5, 6));
  const list = systems.systems();
  expect(flushDirtySram(be, list)).toBe(1);
  expect([...be.readFile(SAV)!]).toEqual([4, 5, 6]);
  expect(sramDirtyCount(be, list)).toBe(0); // now mirrored
  expect(flushDirtySram(be, list)).toBe(0); // nothing dirty → no rewrite
});

test("flushDirtySram is NOT gated on the auto-save preference (unlike flushOnSave)", () => {
  const { be, uc, systems, id } = setup();
  uc.setSramAutoSave("Off"); // would make flushOnSave a no-op
  be.setSram(id, bytes(7, 7));
  expect(flushDirtySram(be, systems.systems())).toBe(1); // still writes — an explicit save on close
  expect([...be.readFile(SAV)!]).toEqual([7, 7]);
});

test("sramDirtyCount / flushDirtySram skip embedded systems (no romPath)", () => {
  const be = new MockBackend("/config");
  const systems = new SystemsStore(be);
  systems.loadMgb(); // embedded, no sibling
  expect(sramDirtyCount(be, systems.systems())).toBe(0);
  expect(flushDirtySram(be, systems.systems())).toBe(0);
  expect(be.log.includes("writeFile")).toBeFalsy();
});

// --- LSDj semantic dirty signature (normalises the per-frame working-RAM clock) ---

const WORK_HOURS = 0x3fb2; // a ticking work-clock byte — LSDj rewrites it every frame; NOT modeled
const TEMPO = 0x3fb4; // a meaningful, modeled byte

test("lsdjSramSignature ignores the ticking work-clock but catches a real edit", () => {
  const orig = savFrom({ workingSong: { settings: { tempo: 150 } } });
  expect(orig.length).toBe(0x20000);

  // A frame tick bumps the work clock (and its checksum) but changes nothing meaningful.
  const ticked = orig.slice();
  ticked[WORK_HOURS] = (ticked[WORK_HOURS] + 7) & 0xff;
  ticked[0x3fb9] = (ticked[0x3fb9] + 7) & 0xff; // totalTimeChecksum
  expect(lsdjSramSignature(ticked)).toBe(lsdjSramSignature(orig)); // same signature
  expect(hashBytes(ticked) === hashBytes(orig)).toBeFalsy(); // but the raw bytes differ

  // A tempo change IS meaningful (a modeled field) → the signature changes.
  const edited = orig.slice();
  edited[TEMPO] = 90;
  expect(lsdjSramSignature(edited) === lsdjSramSignature(orig)).toBeFalsy();
});

test("sramSignature falls back to a whole-SRAM hash for a non-LSDj battery", () => {
  const gb = bytes(1, 2, 3, 4); // not a 128 KiB jk-stamped image
  expect(lsdjSramSignature(gb)).toBe(null);
  expect(sramSignature(gb)).toBe(hashBytes(gb));
});

test("an LSDj cart whose only diff from disk is the ticked clock is NOT dirty", () => {
  const be = new MockBackend("/config");
  const systems = new SystemsStore(be);
  be.seed("/proj/a.gb", gbRom());
  const id = systems.addSystem("/proj/a.gb")!;

  const disk = savFrom({ workingSong: { settings: { tempo: 150 } } });
  be.seed(SAV, disk); // the last saved .sav
  const live = disk.slice();
  live[WORK_HOURS] = (live[WORK_HOURS] + 3) & 0xff; // the clock has since ticked
  be.setSram(id, live);

  // Whole-SRAM hashing would report this dirty; the semantic signature does not.
  expect(sramDirtyCount(be, systems.systems())).toBe(0);

  // A real edit (tempo) makes it dirty again.
  const edited = live.slice();
  edited[TEMPO] = 90;
  be.setSram(id, edited);
  expect(sramDirtyCount(be, systems.systems())).toBe(1);
});

// --- pump lifecycle: independent per-system change tracking + hash pruning on removal ------------------

test("pump (Continuous) writes only the systems whose SRAM changed, per system", () => {
  const { be, uc, systems, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.seed("/proj/b.gb", gbRom());
  const id2 = systems.addSystem("/proj/b.gb")!;
  be.setSram(id, bytes(1));
  be.setSram(id2, bytes(2));
  expect(saver.pump()).toBe(2); // both first-observed → both written
  expect(saver.pump()).toBe(0); // neither changed → no writes
  be.setSram(id2, bytes(3)); // only b changes
  expect(saver.pump()).toBe(1); // only b written (a's persistent hash still matches)
});

test("pump prunes the persistent hash of a removed system (no stale-hash leak)", () => {
  const { be, uc, systems, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.seed("/proj/b.gb", gbRom());
  const id2 = systems.addSystem("/proj/b.gb")!;
  be.setSram(id, bytes(1));
  be.setSram(id2, bytes(2));
  saver.pump(); // tracks a persistent hash for both systems
  const hashes = (saver as unknown as { hashes: Map<number, number> }).hashes;
  expect(hashes.has(id) && hashes.has(id2)).toBe(true);
  expect(hashes.size).toBe(2);

  systems.removeSystem(id2);
  saver.pump(); // pruneDeadHashes drops the removed system's hash
  expect(hashes.has(id2)).toBe(false); // pruned
  expect(hashes.has(id)).toBe(true); // the survivor's hash stays
  expect(hashes.size).toBe(1);
});

// --- the cheap gate in front of the semantic signature ------------------------------------------------
// pump() runs on the frame tick, and sramSignature on an LSDj cart is a full encodeSong(decodeSong(...))
// round-trip. A raw whole-battery hash short-circuits the unchanged case (which, on a live cart, is
// essentially every tick). It must not change WHAT gets written - only how much work deciding costs.

test("pump: the raw-hash gate does not change the semantic verdict for an LSDj cart", () => {
  const { be, uc, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  const song = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
  const sav = savFrom({ activeProjectIndex: 0, projects: [{ name: "GRUB", version: 0, song }] } as never);
  be.setSram(id, sav);
  expect(saver.pump()).toBe(1); // first observation, no file → write

  // Byte-identical: the raw gate answers "nothing moved" without consulting the codec.
  be.setSram(id, sav.slice());
  expect(saver.pump()).toBe(0);

  // A raw change the SEMANTIC signature calls meaningless must STILL not write. This is the case the gate
  // could break: the raw hash sees a difference and lets it through, and lsdjSramSignature has to make the
  // real call exactly as it did before. Same clock bytes the signature's own test uses.
  const ticked = sav.slice();
  ticked[WORK_HOURS] = (ticked[WORK_HOURS] + 7) & 0xff;
  ticked[0x3fb9] = (ticked[0x3fb9] + 7) & 0xff; // totalTimeChecksum
  expect(hashBytes(ticked) === hashBytes(sav)).toBeFalsy(); // the gate really does see a change...
  be.setSram(id, ticked);
  expect(saver.pump()).toBe(0); // ...and the semantic signature still says "nothing to save"

  // A modelled byte IS meaningful, so it writes.
  const edited = ticked.slice();
  edited[TEMPO] = 90;
  be.setSram(id, edited);
  expect(saver.pump()).toBe(1);
});

test("pump: a cold-booted system re-seeds from disk instead of writing its old snapshot back", () => {
  const { be, uc, systems, saver, id } = setup();
  uc.setSramAutoSave("Continuous");
  be.setSram(id, bytes(1, 2, 3));
  expect(saver.pump()).toBe(1); // now holding a persistent hash for `id`

  // Exactly what a Songs-menu edit does: write the new battery, then cold-boot from it. loadSram rebuilds
  // in place and allocates a NEW system id, so the pump must shed the old one's cached hash and
  // first-observe the new one against the file - not resurrect the pre-edit snapshot over it.
  be.writeFile(SAV, bytes(9, 9, 9));
  const newId = systems.loadSram(id, SAV)!;
  expect(newId === id).toBeFalsy(); // the rebuild really did re-id
  be.setSram(newId, bytes(9, 9, 9));

  expect(saver.pump()).toBe(0); // first observation matches disk → seed, no write
  expect([...be.readFile(SAV)!]).toEqual([9, 9, 9]); // the edit survived the tick
});

test("pump(limit) examines at most `limit` systems per tick, round-robin over them all", () => {
  const be = new MockBackend("/config");
  const uc = new UserConfigStore(be);
  uc.setSramAutoSave("Continuous");
  const systems = new SystemsStore(be);
  const saver = new SramAutoSaver(be, systems, uc);
  be.seed("/proj/a.gb", gbRom());
  be.seed("/proj/b.gb", gbRom());
  be.seed("/proj/c.gb", gbRom());
  const ids = ["/proj/a.gb", "/proj/b.gb", "/proj/c.gb"].map((p) => systems.addSystem(p)!);
  ids.forEach((id, i) => be.setSram(id, bytes(i + 1)));

  // Each tick writes exactly ONE system - the per-frame budget the UI relies on.
  expect(saver.pump(1)).toBe(1);
  expect(saver.pump(1)).toBe(1);
  expect(saver.pump(1)).toBe(1);
  // ...and after a full cycle every system has reached disk, so nothing is starved.
  expect([...be.readFile("/proj/a.sav")!]).toEqual([1]);
  expect([...be.readFile("/proj/b.sav")!]).toEqual([2]);
  expect([...be.readFile("/proj/c.sav")!]).toEqual([3]);

  // Steady state costs no writes, and the cursor keeps moving rather than sticking on one system.
  expect(saver.pump(1) + saver.pump(1) + saver.pump(1)).toBe(0);

  // A change on the LAST system is still picked up within one full cycle.
  be.setSram(ids[2], bytes(9));
  expect(saver.pump(1) + saver.pump(1) + saver.pump(1)).toBe(1);
  expect([...be.readFile("/proj/c.sav")!]).toEqual([9]);
});
