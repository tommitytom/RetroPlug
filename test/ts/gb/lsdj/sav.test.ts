// End-to-end LSDj sav fixture loop + generated-zod validation.
//
//   author JSON fixture -> emu.savFromJson -> .sav bytes -> emu.loadRom(rom, sav)
//   -> boot LSDJ from the authored sav
//
// and the generated zod schema accepts valid fixtures / rejects bad ones.
import { test, expect, emu, Mem } from "harness";
import {
  KitInstrumentSchema,
  PhraseSchema,
} from "../../../../build/ui/generated/SavSchema";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";

test("author a sav from JSON and boot LSDJ from it", () => {
  // Author only the fields we care about; the rest take model defaults.
  const savBytes = emu.savFromJson(JSON.stringify({
    workingSong: { formatVersion: 22, settings: { tempo: 150, syncMode: "Lsdj" } },
  }));
  const b = new Uint8Array(savBytes);
  expect(b.length).toBe(0x20000);
  expect(b[0x813e]).toBe(0x6a);  // 'j'  — SRAM init magic written
  expect(b[0x813f]).toBe(0x6b);  // 'k'
  expect(b[0x7fff]).toBe(22);    // working-song format version
  expect(b[0x3fb4]).toBe(150);   // tempo byte (150 stored as-is)

  // Boot LSDJ from the authored sav.
  const sys = emu.loadRom(LSDJ, savBytes);
  emu.runMs(2000);
  const sram = emu.readMemory(sys, Mem.Sram);
  expect(sram.length).toBe(0x20000);
  expect(sram[0x813e]).toBe(0x6a);  // jk still present => our sav was loaded
  expect(sram[0x813f]).toBe(0x6b);
});

test("generated zod accepts valid fixtures and rejects bad ones", () => {
  const goodKit = {
    type: "kit", name: "DRUM", panning: "None", tableMode: "Play", volume: 0xA8,
    kit1: 5, kit2: 0, halfSpeed: false, loop1: "Off", loop2: "Off",
    distortion: "Clip", pitch: 0, length1: 0, offset1: 0, offset2: 0,
  };
  expect(KitInstrumentSchema.safeParse(goodKit).success).toBe(true);
  // bounded sub-byte field (kit1 is 5-bit, lte 31)
  expect(KitInstrumentSchema.safeParse({ ...goodKit, kit1: 200 }).success).toBe(false);
  // bad enum value
  expect(KitInstrumentSchema.safeParse({ ...goodKit, distortion: "Bogus" }).success).toBe(false);
  // wrong discriminator tag
  expect(KitInstrumentSchema.safeParse({ ...goodKit, type: "pulse" }).success).toBe(false);

  // arrays are length-flexible now: the sav codec pads short/omitted arrays to
  // their on-disk length, so the schema only validates element types.
  const goodPhrase = {
    notes: Array(16).fill(0), instruments: Array(16).fill(null),
    commands: Array(16).fill("None"), commandValues: Array(16).fill(0),
  };
  expect(PhraseSchema.safeParse(goodPhrase).success).toBe(true);
  // a short array is accepted (padded on author); a bad element is still rejected
  expect(PhraseSchema.safeParse({ ...goodPhrase, notes: Array(15).fill(0) }).success).toBe(true);
  expect(PhraseSchema.safeParse({ ...goodPhrase, commands: Array(16).fill("Bogus") }).success).toBe(false);
});
