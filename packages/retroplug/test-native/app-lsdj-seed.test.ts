// The load-time LSDj sav-seed, proven end-to-end through the REAL native core. Adding a fresh LSDj
// ROM with no on-disk .sav must NOT cold-boot into the 12–15 s cartridge self-test: the lsdj-sync
// role's onConstruct hook hands the core a valid empty sav (savFromJson) as sramBytes, so the battery
// is initialized at construct. We prove it two ways: (1) the Backend's savFromJson returns a valid
// image stamped with LSDj's `jk` SRAM-init magic; (2) after a store-driven addSystem with no sav, the
// core's battery (readSram) carries that same `jk` magic — i.e. the seed reached a real SameBoy core.
// (A cold-booted fresh cart, self-test not yet run, has a zeroed battery — no `jk`.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { buildAppRegistry } from "../src/appHost";
import { SystemsStore } from "../src/systemsStore";

declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";

// The `jk` SRAM-init magic the codec stamps at 0x813E of a valid LSDj sav (SavCodec kInit).
const JK0 = 0x6a;
const JK1 = 0x6b;
const carriesJk = (b: Uint8Array): boolean => {
  for (let i = 0; i + 1 < b.length; i++) if (b[i] === JK0 && b[i + 1] === JK1) return true;
  return false;
};

test("a fresh LSDj ROM with no sav is seeded a valid empty sav — the real core's battery gets it", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP app-lsdj-seed: LSDj ROM not found at ${LSDJ}`);
    return; // resource-less environment — the devcontainer has it
  }

  // (1) The Backend's savFromJson encodes a valid empty image with the `jk` magic at 0x813E.
  const emptySav = be.savFromJson("{}");
  expect(emptySav.length > 0x8140).toBeTruthy();
  expect(emptySav[0x813e] === JK0 && emptySav[0x813f] === JK1).toBeTruthy();

  // Copy the ROM into a clean writable slot with no sibling .sav, so the fresh-cart path is exercised
  // (a stray .sav next to the resource ROM would legitimately suppress the seed).
  const rom = __CONFIG_DIR__ + "/lsdj-seed.gb";
  const sav = __CONFIG_DIR__ + "/lsdj-seed.sav";
  be.deleteFile(sav); // ensure absent (ignore result)
  const romBytes = be.readFile(LSDJ);
  expect(romBytes != null).toBeTruthy();
  expect(be.writeFile(rom, romBytes!)).toBeTruthy();

  // (2) Build via the store (registry wired → the ROM provider attaches lsdj-sync → its hook runs).
  const store = new SystemsStore(be, () => {}, buildAppRegistry());
  const id = store.addSystem(rom);
  expect(typeof id).toBe("number");
  expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-sync")).toBeTruthy();

  // The seed reached the real SameBoy core: its battery, read at construct (no block rendered), carries
  // the `jk` magic. Without the seed this would be a zeroed cold-boot battery.
  const sram = be.readSram(id!);
  expect(sram != null && sram.length > 0).toBeTruthy();
  expect(carriesJk(sram!)).toBeTruthy();
});
