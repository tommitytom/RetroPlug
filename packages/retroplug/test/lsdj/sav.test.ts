// Phase 4 (pure tier): sav.ts decodes the full 128 KiB image (incl the stored-
// project archive) to the C++ golden and encodes it back byte-identically; the
// 32 KiB early-SRAM path decodes working-song-only. The exhaustive 549-sav
// corpus sweep runs in the native tier (test-native/lsdj-codec-corpus.test.ts).
import { test, expect } from "../../testing/harness";
import { deepEqual } from "./_assert";
import { savBytes } from "./fixtures";
import { decodeSav, encodeSav } from "../../src/lsdj/codec/sav";
import gold499 from "./golden/lsdj499.json";
import goldAll from "./golden/all.json";
import gold834 from "./golden/lsdj834.json";
import gold888 from "./golden/lsdj888.json";

// A focused subset (the full 12-golden matrix is asserted in corpus.test.ts).
const KEYS = ["lsdj499", "all", "lsdj834", "lsdj888"] as const;
const goldens: Record<string, unknown> = { lsdj499: gold499, all: goldAll, lsdj834: gold834, lsdj888: gold888 };

for (const key of KEYS) {
  test(`decodeSav(${key}) matches the full C++ golden (incl stored projects)`, () => {
    deepEqual(decodeSav(savBytes(key)), goldens[key], key);
  });

  test(`encodeSav(${key}): working song byte-identical, archive round-trips at the model level`, () => {
    const sav = savBytes(key);
    const re = encodeSav(decodeSav(sav), sav);
    expect(re.length).toBe(sav.length);
    // The working-song region (0..0x7FFF) is strictly byte-identical.
    for (let i = 0; i < 0x8000; i++) {
      if (re[i] !== sav[i]) throw new Error(`working-song byte mismatch at 0x${i.toString(16)}: got ${re[i]} want ${sav[i]}`);
    }
    // The stored-project archive is re-laid sequentially (as the C++ codec does),
    // so it need not be byte-identical to the original block layout — but it must
    // preserve the model exactly, and re-encoding must be byte-stable.
    deepEqual(decodeSav(re), decodeSav(sav), `${key} model round-trip`);
    const re2 = encodeSav(decodeSav(re), re);
    for (let i = 0; i < re.length; i++) {
      if (re2[i] !== re[i]) throw new Error(`not byte-stable at 0x${i.toString(16)}`);
    }
  });
}

test("a project-less sav re-encodes fully byte-identical", () => {
  // Strip the archive from a content sav -> a project-less image. Its full 128 KiB
  // re-encode (working song + empty archive) is byte-identical.
  const m = decodeSav(savBytes("lsdj499"));
  m.projects = m.projects.map(() => null);
  const noArchive = encodeSav(m); // no template -> canonical empty archive
  const re = encodeSav(decodeSav(noArchive), noArchive);
  for (let i = 0; i < re.length; i++) {
    if (re[i] !== noArchive[i]) throw new Error(`byte mismatch at 0x${i.toString(16)}`);
  }
});

test("all.sav carries its two named stored projects", () => {
  const sav = decodeSav(savBytes("all"));
  const present = sav.projects.filter((p) => p !== null);
  expect(present.length).toBe(2);
  expect(typeof present[0]!.name).toBe("string");
  expect(present[0]!.song.formatVersion).toBe(7);
});

test("a 32 KiB early-SRAM image decodes as working-song-only", () => {
  // Feed only the working-song region: no header/archive -> working-song-only sav.
  const early = savBytes("lsdj499").subarray(0, 0x8000);
  const sav = decodeSav(early);
  expect(sav.activeProjectIndex).toBe(0xff);
  expect(sav.projects.every((p) => p === null)).toBeTruthy();
  deepEqual(sav.workingSong, (gold499 as { workingSong: unknown }).workingSong, "workingSong");
});

test("decodeSav rejects an image without the 'jk' init magic", () => {
  const bad = new Uint8Array(0x20000); // all zero -> no 'jk' at 0x813E
  expect(() => decodeSav(bad)).toThrow();
});

test("savFromJson-style authoring: encodeSav of a default model has the 'jk' magic", () => {
  const sav = decodeSav(savBytes("lsdj499"));
  // wipe projects, re-encode with no template (authoring path)
  sav.projects = sav.projects.map(() => null);
  const out = encodeSav(sav);
  expect(out.length).toBe(0x20000);
  expect(out[0x813e]).toBe(0x6a);
  expect(out[0x813f]).toBe(0x6b);
});
