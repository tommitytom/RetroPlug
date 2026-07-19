// risa DPCM kit ROM support (M5, read/pack/mirror side) — pure-TS tests. The 8 KB banks are produced
// natively (see test-native/risa-kit.test.ts); here we verify the TS reader (bankToModel), the metadata-
// mirror locate + dual-write (setKit), and the DMC decoder. Uses the synthetic risaRomFull() fixture,
// which carries one populated kit ("TEST"/"KIK") + a KIT_META mirror at 0x30000 (past both hint offsets,
// so the bounded scan is exercised).
import { test, expect } from "../../testing/harness";
import { risaRomFull } from "../systems/fixtures";
import { RisaRom, bankToModel, deriveMetaFromBank, dpcmDecode } from "../../src/risa/rom";

// The mirror offsets in risaRomFull (metaBase 0x30000 + 6-byte magic).
const META = 0x30000 + 6;
const PRESENT = META + 512 + 1536; // kit_slot_present[32][16]
const str = (b: Uint8Array) => String.fromCharCode(...b).replace(/[\0 ]+$/, "");

test("RisaRom reads the populated kit and locates the metadata mirror via the bounded scan", () => {
  const rom = RisaRom.fromBytes(risaRomFull());
  expect(rom.hasKitMeta).toBe(true); // found past the hints (0x20010/0x10010) by the scan
  expect(rom.isKitPopulated(0)).toBe(true);
  expect(rom.isKitPopulated(1)).toBe(false);
  expect(rom.kitCount()).toBe(1);
  expect(rom.firstFreeKitIndex()).toBe(1);
  const kits = rom.kits();
  expect(kits.map((k) => k.slot)).toEqual([0]);
  expect(kits[0].name).toBe("TEST");
});

test("bankToModel parses the index table, sample names, and DPCM bytes", () => {
  const model = bankToModel(RisaRom.fromBytes(risaRomFull()).getKitBank(0)!);
  expect(model.name).toBe("TEST");
  expect(model.slots[0]?.name).toBe("KIK");
  expect(model.slots[0]?.rate).toBe(12);
  expect(model.slots[0]?.dpcm.length).toBe(1); // lenReg 0 → 1 byte
  expect(model.slots[1]).toBe(null); // 0xFF index entry
});

test("deriveMetaFromBank builds the three mirror rows from a bank's own bytes", () => {
  const meta = deriveMetaFromBank(RisaRom.fromBytes(risaRomFull()).getKitBank(0)!);
  expect(str(meta.nameBytes)).toBe("TEST");
  expect(str(meta.sampleNamesBytes.slice(0, 3))).toBe("KIK");
  expect(meta.slotPresentBytes[0]).toBe(1);
  expect(meta.slotPresentBytes[1]).toBe(0);
});

test("setKit dual-writes the bank AND the metadata mirror (so the on-device list can't go stale)", () => {
  const rom = RisaRom.fromBytes(risaRomFull());
  const bank = rom.getKitBank(0)!; // the populated "TEST" kit

  rom.setKit(5, bank);
  expect(rom.isKitPopulated(5)).toBe(true);
  expect([...rom.getKitBank(5)!]).toEqual([...bank]);
  // The mirror row for slot 5 was written directly (verified at the known mirror offset).
  const out = rom.bytes();
  expect(str(out.slice(META + 5 * 16, META + 5 * 16 + 4))).toBe("TEST"); // kit_names[5]
  expect(out[PRESENT + 5 * 16]).toBe(1); // kit_slot_present[5][0]
  // A re-parse now lists both kits — proving the splice is self-consistent.
  expect(RisaRom.fromBytes(out).kits().map((k) => k.slot)).toEqual([0, 5]);

  rom.clearKitBank(0);
  expect(rom.isKitPopulated(0)).toBe(false);
  expect(rom.bytes()[PRESENT + 0 * 16]).toBe(0); // mirror present bit cleared too
});

test("dpcmDecode inverts the ±2 delta (LSB-first, <127/>0 guards)", () => {
  const up = dpcmDecode(new Uint8Array([0xff, 0xff])); // 16 one-bits → counter ramps up
  expect(up.length).toBe(16);
  expect(up[0] > 0).toBe(true); // 64 → 66
  expect(up[7] > up[0]).toBe(true); // still ramping
  const down = dpcmDecode(new Uint8Array([0x00])); // zero-bits → counter ramps down
  expect(down[0] < 0).toBe(true); // 64 → 62
});
