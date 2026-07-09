// FOUNDATION F1/F2: the Mesen (NES) backend joins the live snapshot plane like SameBoy. Against a
// REAL Mesen core: readState must republish as the core runs (not freeze at the boot seed), a ROM
// that passes the NES magic gate but fails Mesen's LoadRom must fail the construct (no zombie tile),
// and a battery cart's SRAM must read back through the registry (F2). n8-midi.nes is committed
// in-repo; the battery NES fixture is authored inline (no real-core battery ROM ships).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { SystemsStore } from "../src/systemsStore";
import { buildAppRegistry } from "../src/appHost";
import { nesRom, nesRomBattery } from "../test/systems/fixtures";

declare const __REPO_RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const bytesEqual = (a: Uint8Array, b: Uint8Array): boolean =>
  a.length === b.length && a.every((v, i) => v === b[i]);

test("F1: a live NES core republishes its savestate — readState isn't frozen at the boot seed", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  const id = (project.systems.loadRom(NES) as { system: number }).system;
  expect(project.systems.view()[0].platform).toBe("nes");

  // Boot seed: the registry captured this at construct (before any block ran).
  const boot = be.readState(id);
  expect(boot != null && boot.length > 0).toBeTruthy();

  // Advance the core well past the ~0.5s snapshot interval so the registry republishes the LIVE state.
  audio.renderAudio(1500);
  const live = be.readState(id);
  expect(live != null && live.length > 0).toBeTruthy();

  // Before F1 (no enableStateSnapshot + the registry's == guard) these were byte-identical — the core
  // ran but the published state stayed the power-on seed, silently breaking Save State / Duplicate.
  expect(bytesEqual(boot!, live!)).toBeFalsy();
});

test("F1: a ROM that passes the NES magic gate but fails Mesen LoadRom fails the construct", () => {
  const be = createRealBackend();
  const bad = __CONFIG_DIR__ + "/roms/bad.nes";
  be.writeFile(bad, nesRom()); // iNES magic, but 0 PRG banks → Mesen rejects it at LoadRom

  const store = new SystemsStore(be, () => {}, buildAppRegistry());
  const id = store.addSystem(bad);
  expect(id).toBe(null); // the activation guard rejects the build instead of adopting a dead system
  expect(store.view().length).toBe(0);
});

test("F2: a battery NES cart's SRAM reads back through the registry; reset carries it forward", () => {
  const be = createRealBackend();
  const rom = __CONFIG_DIR__ + "/roms/batt.nes";
  be.writeFile(rom, nesRomBattery());

  const store = new SystemsStore(be, () => {}, buildAppRegistry());
  const a = store.addSystem(rom);
  expect(typeof a).toBe("number");
  expect(store.view()[0].platform).toBe("nes");

  // Before F2, readSram was ALWAYS null for Mesen: the registry only sliced SRAM out of the savestate,
  // and Mesen's streamed format exposes no SRAM region there. Now it's sourced from the live core.
  const sram = be.readSram(a as number);
  expect(sram != null && sram!.length > 0).toBeTruthy();

  // saveSram writes the live battery to disk (real bytes reached the .sav).
  const sramPath = __CONFIG_DIR__ + "/batt.sav";
  expect(store.saveSram(a as number, sramPath)).toBeTruthy();
  const onDisk = be.readFile(sramPath);
  expect(onDisk != null && onDisk!.length > 0).toBeTruthy();

  // New SRAM zeros the battery; Reset then carries the live (blanked) battery forward — a cold boot
  // (dropping the live battery) would re-read the cart's power-up fill and fail the all-zero check.
  const blanked = store.newSram(a as number)!;
  expect(be.readSram(blanked)!.every((b) => b === 0)).toBeTruthy();
  const rst = store.reset(blanked)!;
  expect(be.readSram(rst)!.every((b) => b === 0)).toBeTruthy();
});
