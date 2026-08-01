// A tracker cart's OWN name — its internal ROM title / version marker, distinct from the on-disk filename.
// LSDj reads the GB cartridge title at 0x134; risa scans the PRG for the ASCII "RISA V<x>.<y>.<z>" marker.
import { test, expect } from "../../testing/harness";
import { resolveTracker } from "../../src/tracker";
import { lsdjRom } from "../systems/fixtures";

const lsdjTracker = resolveTracker([{ kind: "lsdj-sync", config: {} }])!;
const risaTracker = resolveTracker([{ kind: "risa", config: {} }])!;

test("LSDj romName reads the versioned cartridge title (not the filename)", () => {
  expect(lsdjTracker.romName(lsdjRom("LSDJ-V9.4.2"))).toBe("LSDj v9.4.2");
  expect(lsdjTracker.romName(lsdjRom("LSDJ-V9.2.L"))).toBe("LSDj v9.2.L"); // a letter patch level is preserved
  expect(lsdjTracker.romName(lsdjRom("LSDJ"))).toBe(null); // an old bare title carries no version
});

test("risa romName reads the embedded 'RISA V' PRG marker", () => {
  const rom = new Uint8Array(0x400);
  const marker = "RISA V2.2.1";
  for (let i = 0; i < marker.length; i++) rom[0x100 + i] = marker.charCodeAt(i);
  expect(risaTracker.romName(rom)).toBe("risa v2.2.1");
  expect(risaTracker.romName(new Uint8Array(0x400))).toBe(null); // no marker → null
});
