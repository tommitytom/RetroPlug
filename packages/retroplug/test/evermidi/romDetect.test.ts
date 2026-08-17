// EverMIDI SIG-block detection tests: find the "evermidi-n8" head marker + read its semver, and reject
// non-EverMIDI buffers. Pure byte-level — no emulator or real ROM.
import { test, expect } from "../../testing/harness";
import { everMidiInfo, isEverMidiRomHeader, EVERMIDI_MARKER } from "../../src/evermidi/romDetect";
import { everMidiRom, nesRom, garbage } from "../systems/fixtures";

// A header carrying the marker + a semver at `at`.
function sigHeader(semver: [number, number, number], at = 0x10): Uint8Array {
  const h = new Uint8Array(0x150);
  let p = at;
  for (let i = 0; i < EVERMIDI_MARKER.length; i++) h[p++] = EVERMIDI_MARKER.charCodeAt(i);
  h[p++] = semver[0]; h[p++] = semver[1]; h[p++] = semver[2];
  return h;
}

test("everMidiInfo reads the semver from the fixture ROM's SIG block", () => {
  const rom = everMidiRom();
  expect(everMidiInfo(rom)).toEqual({ semver: [0, 1, 0] });
  expect(isEverMidiRomHeader(rom)).toBe(true);
});

test("the marker is found anywhere in the 0x150 scan window", () => {
  expect(everMidiInfo(sigHeader([1, 2, 3], 0x40))).toEqual({ semver: [1, 2, 3] });
});

test("non-EverMIDI buffers return null / false", () => {
  for (const buf of [nesRom(), garbage()]) {
    expect(everMidiInfo(buf)).toBe(null);
    expect(isEverMidiRomHeader(buf)).toBe(false);
  }
});
