// BlipToaster SIG-block detection tests: find the "bliptoaster" head marker + read its semver, and reject
// non-BlipToaster buffers. Pure byte-level — no emulator or real ROM.
import { test, expect } from "../../testing/harness";
import {
  blipToasterInfo,
  isBlipToasterRomHeader,
  blipToasterChip,
  BLIPTOASTER_MARKER,
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

test("blipToasterChip reads the build off the iNES mapper", () => {
  const rom = blipToasterRom();
  expect(blipToasterChip(withMapper(rom, 5))).toBe("mmc5");
  expect(blipToasterChip(withMapper(rom, 19))).toBe("n163");
  expect(blipToasterChip(withMapper(rom, 24))).toBe("vrc6");
  expect(blipToasterChip(withMapper(rom, 85))).toBe("vrc7");
  // FME-7 (69) is the base build's kit-banking mapper AND the Sunsoft 5B mapper, and the two ROMs are
  // otherwise header-identical, so it resolves to the subset that can never be wrong.
  expect(blipToasterChip(withMapper(rom, 69))).toBe("2a03");
  expect(blipToasterChip(rom)).toBe("2a03"); // the fixture is NROM
});

test("the provider records the build on the `bliptoaster` role", () => {
  const reg = buildAppRegistry();
  const chipOf = (rom: Uint8Array): string | undefined => {
    const role = reg.defaultRoles("mesen", "nes", rom).find((r) => r.kind === "bliptoaster");
    return (role?.config as { chip?: string } | undefined)?.chip;
  };
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
