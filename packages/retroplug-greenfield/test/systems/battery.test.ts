// The per-system `battery` flag: derived (not serialized) from the ROM header at construct, and surfaced
// on the SystemView so the UI can gate the "Save SRAM" affordances (a battery-less cart would only ever
// write a stray empty .sav). romHasBattery is the pure reader; the store wires it through detectBattery.
import { test, expect } from "../../testing/harness";
import { romHasBattery } from "../../src/platform";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { gbRom, gbRomBattery, nesRom, nesRomBattery, gbaRom } from "./fixtures";

// A raw GB header prefix carrying a given cartridge-type byte at $147.
function gbHeader(cartType: number): Uint8Array {
  const b = new Uint8Array(0x150);
  b[0x147] = cartType;
  return b;
}
// A raw iNES header carrying a given flags6 byte (byte 6).
function nesHeader(flags6: number): Uint8Array {
  const b = new Uint8Array(0x10);
  b[6] = flags6;
  return b;
}

function reg(): RoleRegistry {
  const r = new RoleRegistry();
  registerCoreRoles(r);
  return r;
}

test("romHasBattery: NES reads iNES flags6 bit 1", () => {
  expect(romHasBattery(nesRom(), "nes")).toBe(false); // flags6 = 0
  expect(romHasBattery(nesRomBattery(), "nes")).toBe(true); // flags6 = 0x12 (MMC1 | battery)
  expect(romHasBattery(nesHeader(0x02), "nes")).toBe(true); // just the battery bit
  expect(romHasBattery(nesHeader(0x01), "nes")).toBe(false); // a mapper bit, no battery
});

test("romHasBattery: GB reads the cartridge type at $147", () => {
  expect(romHasBattery(gbRom(), "gb")).toBe(false); // 0x00 ROM-only
  expect(romHasBattery(gbRomBattery(), "gb")).toBe(true); // 0x03 MBC1+RAM+BATTERY
  expect(romHasBattery(gbHeader(0x01), "gb")).toBe(false); // MBC1, no battery
  expect(romHasBattery(gbHeader(0x1b), "gb")).toBe(true); // MBC5+RAM+BATTERY (typical LSDj)
});

test("romHasBattery: GBA and short headers never wrongly grey a save", () => {
  expect(romHasBattery(gbaRom(), "gba")).toBe(true); // GBA save-type detection is heuristic → default on
  expect(romHasBattery(new Uint8Array(4), "nes")).toBe(false); // too short for byte 6
  expect(romHasBattery(new Uint8Array(4), "gb")).toBe(false); // too short for $147
});

test("SystemView.battery is derived from the ROM header at construct", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/plain.nes", nesRom());
  be.seed("/roms/batt.nes", nesRomBattery());
  be.seed("/roms/plain.gb", gbRom());
  be.seed("/roms/batt.gb", gbRomBattery());
  const s = new SystemsStore(be, () => {}, reg());
  const byId = (id: number) => s.view().find((v) => v.id === id)!;

  const plainNes = s.addSystem("/roms/plain.nes") as number;
  const battNes = s.addSystem("/roms/batt.nes") as number;
  const plainGb = s.addSystem("/roms/plain.gb") as number;
  const battGb = s.addSystem("/roms/batt.gb") as number;

  expect(byId(plainNes).battery).toBe(false);
  expect(byId(battNes).battery).toBe(true);
  expect(byId(plainGb).battery).toBe(false);
  expect(byId(battGb).battery).toBe(true);
});
