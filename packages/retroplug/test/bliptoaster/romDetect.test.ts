// BlipToaster SIG-block detection tests: find the "bliptoaster" head marker + read its semver, and reject
// non-BlipToaster buffers. Pure byte-level — no emulator or real ROM.
import { test, expect } from "../../testing/harness";
import {
  blipToasterInfo,
  isBlipToasterRomHeader,
  blipToasterChip,
  BLIPTOASTER_MARKER,
  BLIPTOASTER_CHIP_SCAN_LEN,
} from "../../src/bliptoaster/romDetect";
import { buildAppRegistry } from "../../src/appHost";
import { blipToasterRom, nesRom, garbage } from "../systems/fixtures";

// A header carrying the marker + a semver at `at`.
function sigHeader(semver: [number, number, number], at = 0x10): Uint8Array {
  const h = new Uint8Array(0x150);
  let p = at;
  for (let i = 0; i < BLIPTOASTER_MARKER.length; i++) h[p++] = BLIPTOASTER_MARKER.charCodeAt(i);
  h[p++] = semver[0]; h[p++] = semver[1]; h[p++] = semver[2];
  return h;
}

test("blipToasterInfo reads the semver from the fixture ROM's SIG block", () => {
  const rom = blipToasterRom();
  expect(blipToasterInfo(rom)).toEqual({ semver: [0, 1, 0] });
  expect(isBlipToasterRomHeader(rom)).toBe(true);
});

test("the marker is found anywhere in the 0x150 scan window", () => {
  expect(blipToasterInfo(sigHeader([1, 2, 3], 0x40))).toEqual({ semver: [1, 2, 3] });
});

test("non-BlipToaster buffers return null / false", () => {
  for (const buf of [nesRom(), garbage()]) {
    expect(blipToasterInfo(buf)).toBe(null);
    expect(isBlipToasterRomHeader(buf)).toBe(false);
  }
});

// --- which build is it? (drives the DAW parameter map — see src/parameterMap.ts) ---

/** The fixture ROM with its iNES mapper number rewritten. */
function withMapper(rom: Uint8Array, mapper: number): Uint8Array {
  const b = rom.slice();
  b[6] = (b[6] & 0x0f) | ((mapper & 0x0f) << 4);
  b[7] = (b[7] & 0x0f) | (mapper & 0xf0);
  return b;
}

/** Write a NUL-terminated ASCII label into `rom`, the way its RODATA carries AUDIO_CHIP_NAME for
 *  `blit_str(0, 1, AUDIO_CHIP_NAME)` (the ROM repo's src/ui/ui.c). The fixture ROM deliberately carries
 *  NO label, so every test that omits this one is exercising the mapper fallback. */
function withLabel(rom: Uint8Array, label: string, at = 0x2f00): Uint8Array {
  const b = rom.slice();
  for (let i = 0; i < label.length; i++) b[at + i] = label.charCodeAt(i);
  b[at + label.length] = 0;
  return b;
}

test("the chip label separates the two mapper-69 builds, which nothing in the header can", () => {
  const base = withMapper(blipToasterRom(), 69);
  const a = withLabel(base, "2A03");
  const b = withLabel(base, "S5B");

  // Identical headers - same mapper, same flags, same size. This is the real ROMs' situation exactly.
  expect([...a.subarray(0, 16)]).toEqual([...b.subarray(0, 16)]);
  expect(a.length).toBe(b.length);

  expect(blipToasterChip(a)).toBe("2a03");
  expect(blipToasterChip(b)).toBe("s5b");
});

test("every build's label is recognised, and the label outranks the mapper", () => {
  const rom = blipToasterRom();
  const cases = [
    ["2A03", "2a03"], ["VRC6", "vrc6"], ["VRC7", "vrc7"],
    ["S5B", "s5b"], ["N163", "n163"], ["MMC5", "mmc5"],
  ] as const;
  for (const [label, chip] of cases) expect(blipToasterChip(withLabel(rom, label))).toBe(chip);

  // The ROM's own statement wins over a mapper that says otherwise.
  expect(blipToasterChip(withLabel(withMapper(rom, 85), "MMC5"))).toBe("mmc5");
});

test("only a NUL-TERMINATED label matches, so the `VRC7 PATCH` heading is not one", () => {
  // The real VRC7 ROM contains "VRC7" twice: this UI heading and the chip label. Alone, the heading
  // must not count - otherwise the terminator check is doing nothing and any 6502 byte run could match.
  const heading = withLabel(withMapper(blipToasterRom(), 69), "VRC7 PATCH");
  expect(blipToasterChip(heading)).toBe("2a03"); // fell through to the mapper

  // With both present, as the shipped ROM has them, the terminated one decides.
  expect(blipToasterChip(withLabel(heading, "VRC7", 0x3000))).toBe("vrc7");
});

test("two different chip labels are inconclusive rather than a coin flip", () => {
  const both = withLabel(withLabel(withMapper(blipToasterRom(), 85), "VRC6"), "MMC5", 0x3000);
  // Neither label - the mapper, so a future build that names another chip in a string degrades to the
  // old behaviour instead of confidently reporting the wrong one.
  expect(blipToasterChip(both)).toBe("vrc7");
});

test("blipToasterChip falls back to the iNES mapper when the label is out of reach", () => {
  const rom = blipToasterRom();
  expect(blipToasterChip(withMapper(rom, 5))).toBe("mmc5");
  expect(blipToasterChip(withMapper(rom, 19))).toBe("n163");
  expect(blipToasterChip(withMapper(rom, 24))).toBe("vrc6");
  expect(blipToasterChip(withMapper(rom, 85))).toBe("vrc7");
  // FME-7 (69) is the base build's kit-banking mapper AND the Sunsoft 5B mapper, so unlabelled it
  // resolves to the subset that can never be wrong.
  expect(blipToasterChip(withMapper(rom, 69))).toBe("2a03");
  expect(blipToasterChip(rom)).toBe("2a03"); // the fixture is NROM

  // A caller holding only a header prefix gets the mapper answer, not a wrong label answer.
  const labelled = withLabel(withMapper(rom, 85), "MMC5");
  expect(blipToasterChip(labelled.subarray(0, 0x150))).toBe("vrc7");
  // Nor is a label past the PRG region read - the kit banks start at 0x4010 and are not code.
  expect(blipToasterChip(withLabel(withMapper(rom, 85), "MMC5", BLIPTOASTER_CHIP_SCAN_LEN))).toBe("vrc7");
});

test("the provider records the build on the `bliptoaster` role", () => {
  const reg = buildAppRegistry();
  const chipOf = (rom: Uint8Array): string | undefined => {
    const role = reg.defaultRoles("mesen", "nes", rom).find((r) => r.kind === "bliptoaster");
    return (role?.config as { chip?: string } | undefined)?.chip;
  };
  expect(chipOf(withLabel(withMapper(blipToasterRom(), 69), "S5B"))).toBe("s5b");
  expect(chipOf(withMapper(blipToasterRom(), 85))).toBe("vrc7");
  expect(chipOf(withMapper(blipToasterRom(), 24))).toBe("vrc6");
  expect(chipOf(blipToasterRom())).toBe("2a03");
  // a non-BlipToaster NES ROM gets no such role at all
  expect(reg.defaultRoles("mesen", "nes", nesRom()).some((r) => r.kind === "bliptoaster")).toBe(false);
});

// The marker is a wire contract with the ROM repo's rom/src/core/sig.s, and it has moved twice ("EVERMIDI"
// -> "evermidi-n8" -> "bliptoaster"). Pin the exact bytes here so a drift is a failing test rather than a
// ROM that silently loses its Kits/Fonts/Themes menu. The pre-rename spellings must NOT be detected.
test("the marker is exactly the bytes the ROM bakes, and superseded markers are rejected", () => {
  expect(BLIPTOASTER_MARKER).toBe("bliptoaster");

  const withMarker = (mark: string): Uint8Array => {
    const h = new Uint8Array(0x150);
    for (let i = 0; i < mark.length; i++) h[0x10 + i] = mark.charCodeAt(i);
    h[0x10 + mark.length] = 0; h[0x11 + mark.length] = 1; h[0x12 + mark.length] = 0;
    return h;
  };
  for (const old of ["EVERMIDI", "evermidi-n8"]) expect(isBlipToasterRomHeader(withMarker(old))).toBe(false);
});

// The SIG block is padded upstream to a fixed 16 bytes with $FF after the semver. Reading must stop at the
// semver and not mistake the padding for further fields.
test("the $FF padding after the semver is ignored", () => {
  const h = new Uint8Array(0x150).fill(0);
  let p = 0x10;
  for (let i = 0; i < BLIPTOASTER_MARKER.length; i++) h[p++] = BLIPTOASTER_MARKER.charCodeAt(i);
  h[p++] = 0; h[p++] = 1; h[p++] = 0;
  h.fill(0xff, p, 0x10 + 16); // pad to the fixed 16-byte block
  expect(blipToasterInfo(h)).toEqual({ semver: [0, 1, 0] });
});
