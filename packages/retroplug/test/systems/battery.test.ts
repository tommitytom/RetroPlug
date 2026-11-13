// The per-system `battery` flag: derived (not serialized) from the ROM header at construct, and surfaced
// on the SystemView so the UI can gate the "Save SRAM" affordances (a battery-less cart would only ever
// write a stray empty .sav). romHasBattery is the pure reader; the store wires it through detectBattery.
import { test, expect } from "@retroplug/testing";
import { romHasBattery } from "../../src/platform";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "@retroplug/core/systemRoles";
import { registerCoreRoles } from "../_coreRoles";
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

test("romHasBattery: SMS/GG default on - nothing in the header says otherwise", () => {
  // Sega 8-bit carts carry NO battery bit: the cart either wires up SRAM or it doesn't, and the ROM
  // can't say which. Pinned because smsggdj IS a battery cart and its songs live in that SRAM, so a
  // regression to `false` here would grey out the only rows that can save a user's work.
  expect(romHasBattery(new Uint8Array(0x8000), "sms")).toBe(true);
  expect(romHasBattery(new Uint8Array(0x8000), "gg")).toBe(true);
  expect(romHasBattery(new Uint8Array(0), "sms")).toBe(true); // even with no header read at all
});

// A Mega Drive header prefix carrying `flag` as the RAM flag byte at $1B2 (the "RA" marker is at $1B0).
function mdHeader(flag: number, len = 0x200): Uint8Array {
  const b = new Uint8Array(len);
  b[0x1b0] = 0x52; // 'R'
  b[0x1b1] = 0x41; // 'A'
  b[0x1b2] = flag;
  return b;
}

// A Mega Drive ROM image the store classifies as one: the console name at $100, and the same RA block at $1B0. 0x4000
// is longer than the 0x150-byte header the store would read without the console's provider prefix, so the 0x0F answer
// below pins that read (a 0x150-byte prefix is too short to hold $1B2 and answers true by policy).
function mdRom(flag: number, len = 0x4000): Uint8Array {
  const b = new Uint8Array(len);
  const name = "SEGA MEGA DRIVE ";
  for (let i = 0; i < name.length; i++) b[0x100 + i] = name.charCodeAt(i);
  b[0x1b0] = 0x52; // 'R'
  b[0x1b1] = 0x41; // 'A'
  b[0x1b2] = flag;
  return b;
}

test("romHasBattery: MD reads $1B0's RA block", () => {
  expect(romHasBattery(mdHeader(0xf8), "md")).toBe(true); // odd-byte SRAM, genmddj's own header
  expect(romHasBattery(mdHeader(0x20), "md")).toBe(true); // even-byte SRAM
  expect(romHasBattery(mdHeader(0x0f), "md")).toBe(false); // "no RAM" in the ROM DB's nibble
  // Too short to see the field at all: never wrongly greyed.
  expect(romHasBattery(new Uint8Array(0x150), "md")).toBe(true);
});

test("SystemView.battery is derived from the ROM header at construct", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/plain.nes", nesRom());
  be.seed("/roms/batt.nes", nesRomBattery());
  be.seed("/roms/plain.gb", gbRom());
  be.seed("/roms/batt.gb", gbRomBattery());
  be.seed("/roms/batt.md", mdRom(0xf8));
  be.seed("/roms/plain.md", mdRom(0x0f));
  const s = new SystemsStore(be, () => {}, reg());
  const byId = (id: number) => s.view().find((v) => v.id === id)!;

  const plainNes = s.addSystem("/roms/plain.nes") as number;
  const battNes = s.addSystem("/roms/batt.nes") as number;
  const plainGb = s.addSystem("/roms/plain.gb") as number;
  const battGb = s.addSystem("/roms/batt.gb") as number;
  const battMd = s.addSystem("/roms/batt.md") as number;
  const plainMd = s.addSystem("/roms/plain.md") as number;

  expect(byId(plainNes).battery).toBe(false);
  expect(byId(battNes).battery).toBe(true);
  expect(byId(plainGb).battery).toBe(false);
  expect(byId(battGb).battery).toBe(true);
  // $1B2 is past the short header: the store reads the console's own 0x10000-byte prefix, which is what this pins.
  expect(byId(battMd).battery).toBe(true);
  expect(byId(plainMd).battery).toBe(false);
});
