// Regression: a live SameBoy model switch must not freeze the published savestate / SRAM.
//
// The state snapshot slot is sized, and its region table captured, when the system is BUILT. A model
// switch rebuilds the core in place (Engine::applyConfigField -> restartEmulator) while keeping the same
// SystemId, so before the fix the slot kept describing the dead core: a larger capture overflowed it and
// publishStateSnapshot skipped forever, freezing readState/readSram at their pre-switch values. Downstream
// that is real data loss - the SRAM auto-save mirror stops tracking the battery, and a project export
// captures a savestate from a model that can no longer restore it.
//
// Why the existing coverage missed it: the default model is cgbC and the one live-switch test goes
// cgbC -> dmgB, which SHRINKS and therefore fits. Seeing the overflow needs a DMG-family model at build
// and a switch UP. These tests construct on dmgB via the construct-time `settings` blob.
//
// The WRAM plane was already fixed this way (lsdj-wram-seam.test.ts); this is its state/SRAM twin.
// MODEL_VALUES indices: 1 = dmgB, 3 = sgb, 9 = cgbC, 12 = agb.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";

declare const __RESOURCES_DIR__: string;
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7;

const DMG_B = 1, SGB = 3, CGB_C = 9, AGB = 12;
const DMG_WRAM = 0x2000, CGB_WRAM = 0x8000;
/** CGB ram+vram (0x8000 + 0x4000) minus DMG's (0x2000 + 0x2000) - the only model-dependent term in a
 *  savestate for a fixed cart, so a dmgB -> cgbC switch grows the blob by exactly this. */
const RAM_VRAM_DELTA = 0x8000;

const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;
const SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "None", tempo: 128 },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [pulse],
  },
};

const eq = (a: Uint8Array, b: Uint8Array): boolean => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};

/** Build a system on an explicit model. `settings` is the native SameBoyRoleConfig (integer fields),
 *  applied AT CONSTRUCT - which is what sizes the snapshot slot for that model. */
const buildOn = (be: ReturnType<typeof createRealBackend>, id: number, model: number): boolean =>
  be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom(SONG),
    settings: JSON.stringify({ model }),
  }, id);

const requireRom = (be: ReturnType<typeof createRealBackend>, name: string): void => {
  if (!be.fileExists(LSDJ)) skip(`${name}: LSDj ROM not found at ${LSDJ}`);
};

test("readState tracks a live model switch that GROWS the savestate (dmgB → cgbC), never freezing", () => {
  const be = createRealBackend();
  requireRom(be, "state-model-grow");
  const audio = createAudioDriver();
  const id = 101;
  expect(buildOn(be, id, DMG_B)).toBeTruthy();

  audio.renderAudio(2000);
  const dmgState = be.readState(id)!;
  expect(dmgState.length > 0).toBeTruthy();
  expect(be.readRam(id)!.length).toBe(DMG_WRAM); // corroborates the core really booted as DMG

  // Switch UP: the CGB core's savestate is exactly RAM_VRAM_DELTA larger than the DMG one it replaces.
  expect(be.applyRoleConfig(id, "sameboy", { model: CGB_C })).toBeTruthy();
  audio.renderAudio(2000);

  const cgbState = be.readState(id)!;
  console.log(`[state-model-switch] dmg=${dmgState.length} cgb=${cgbState.length} delta=${cgbState.length - dmgState.length}`);
  expect(be.readRam(id)!.length).toBe(CGB_WRAM); // the already-fixed WRAM plane agrees the switch happened
  // Before the fix this was still dmgState.length: the capture overflowed the DMG-sized slot and every
  // publish was skipped, so readState returned the pre-switch blob forever.
  expect(cgbState.length).toBe(dmgState.length + RAM_VRAM_DELTA);

  // Freshness, independent of the size arithmetic: a frozen slot hands back byte-identical blobs.
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(1600); // past the 0.5s publish interval, with the cart running
  expect(eq(be.readState(id)!, cgbState)).toBeFalsy();

  be.removeSystem(id);
});

test("readSram stays correct across a switch that MOVES the SRAM offset (dmgB → sgb)", () => {
  const be = createRealBackend();
  requireRom(be, "state-model-sgb");
  const audio = createAudioDriver();
  const id = 102;
  expect(buildOn(be, id, DMG_B)).toBeTruthy();

  audio.renderAudio(2000);
  const dmgState = be.readState(id)!;
  const dmgSram = be.readSram(id)!;
  expect(dmgSram.length > 0).toBeTruthy();

  // An HLE-SGB model adds a ~74KB section to the savestate AHEAD of the cart-RAM blob, so unlike a
  // DMG<->CGB switch this genuinely MOVES Sram.offset. A fix that only widened the slot (without
  // re-reading the region table) would slice here at the stale, too-low offset and hand the SRAM
  // auto-save mirror the SGB section's bytes as if they were the battery.
  expect(be.applyRoleConfig(id, "sameboy", { model: SGB })).toBeTruthy();
  audio.renderAudio(2000);

  const sgbState = be.readState(id)!;
  console.log(`[state-model-switch] dmg=${dmgState.length} sgb=${sgbState.length} delta=${sgbState.length - dmgState.length}`);
  expect(sgbState.length > dmgState.length + 70000).toBeTruthy(); // the HLE-SGB section landed

  // The published battery must still be the battery: same length, and byte-identical to a live read.
  const sgbSram = be.readSram(id)!;
  expect(sgbSram.length).toBe(dmgSram.length); // cart-derived, so it never moves with the model
  expect(eq(sgbSram, be.readMemory(id, MemoryRegion.Sram)!)).toBeTruthy();

  be.removeSystem(id);
});

// Anti-rot: hardcodes no size at all. Switching INTO a model must be indistinguishable from being BUILT
// on it, which is what keeps the headroom constant honest if SameBoy's savestate layout ever changes.
test("a switched-into model publishes the same state size as one constructed on it", () => {
  const be = createRealBackend();
  requireRom(be, "state-model-sweep");
  const audio = createAudioDriver();
  const models = [DMG_B, CGB_C, SGB, AGB];
  let id = 110;

  const live = id++;
  expect(buildOn(be, live, DMG_B)).toBeTruthy(); // start on the SMALLEST family, so every switch grows

  for (const m of models) {
    const fresh = id++;
    expect(buildOn(be, fresh, m)).toBeTruthy();
    audio.renderAudio(1600);
    const built = be.readState(fresh)!.length;
    be.removeSystem(fresh); // a live system is emulated by every later renderAudio in this file

    expect(be.applyRoleConfig(live, "sameboy", { model: m })).toBeTruthy();
    audio.renderAudio(1600);
    const switched = be.readState(live)!.length;

    console.log(`[state-model-switch] model=${m} built=${built} switched=${switched}`);
    expect(switched).toBe(built);
  }

  be.removeSystem(live);
});
