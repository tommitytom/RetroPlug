// Phase 3 gate: song.ts decode matches the C++ reference (frozen goldens) and
// encode is a byte-identical inverse, across the format/branch surface (fmt3
// command remap + fmt<11 fill; fmt7 archive; fmt11; fmt16 synth-nibble). The
// exhaustive per-format corpus sweep (fmt 0..22, all 549 savs) runs in the
// native tier (test-native/lsdj-codec-corpus.test.ts).
import { test, expect } from "../../testing/harness";
import { deepEqual } from "./_assert";
import { savBytes } from "./fixtures";
import { decodeSong, encodeSong } from "../../src/lsdj/codec/song";
import gold499 from "./golden/lsdj499.json";
import goldAll from "./golden/all.json";
import gold834 from "./golden/lsdj834.json";
import gold888 from "./golden/lsdj888.json";

// A focused subset spanning fmt3/7/11/16 (the full branch matrix + all 12 goldens
// are asserted in corpus.test.ts).
const KEYS = ["lsdj499", "all", "lsdj834", "lsdj888"] as const;
const goldens: Record<string, { workingSong: unknown }> = {
  lsdj499: gold499,
  all: goldAll,
  lsdj834: gold834,
  lsdj888: gold888,
};

for (const key of KEYS) {
  test(`decodeSong(${key}) matches the C++ golden working song`, () => {
    const body = savBytes(key).subarray(0, 0x8000);
    deepEqual(decodeSong(body), goldens[key].workingSong, `${key}.workingSong`);
  });

  test(`encodeSong(${key}) is a byte-identical inverse (template round-trip)`, () => {
    const body = savBytes(key).subarray(0, 0x8000);
    const re = encodeSong(decodeSong(body), body);
    expect(re.length).toBe(0x8000);
    for (let i = 0; i < 0x8000; i++) {
      if (re[i] !== body[i]) throw new Error(`byte mismatch at 0x${i.toString(16)}: got ${re[i]} want ${body[i]}`);
    }
  });
}

test("no-template encode of a decoded song round-trips through decode (fmt16)", () => {
  const body = savBytes("lsdj888").subarray(0, 0x8000);
  const song = decodeSong(body);
  // A no-template re-encode regenerates sentinels/defaults; it must still decode
  // back to the same semantic model.
  const fresh = encodeSong(song); // no template
  deepEqual(decodeSong(fresh), song, "reencoded");
});

test("wave speed truncates to a byte (fmt>=7 static_cast<Byte>)", () => {
  // lsdj888 instrument 15 is a wave whose stored speed byte is 0xFF -> +4 -> 3.
  const song = decodeSong(savBytes("lsdj888").subarray(0, 0x8000));
  const inst = song.instruments[15];
  expect(inst?.type).toBe("wave");
  if (inst?.type === "wave") expect(inst.speed).toBe(3);
});

test("fmt<8 phrase commands use the raw (non-B-remapped) encoding round-trip", () => {
  // lsdj499 is fmt3; every allocated phrase command must round-trip byte-identically
  // (covered by the byte-identity test above) AND decode to a valid Command name.
  const song = decodeSong(savBytes("lsdj499").subarray(0, 0x8000));
  expect(song.formatVersion).toBe(3);
  const withCmd = song.phrases.find((p) => p && p.commands.some((c) => c !== "None"));
  expect(!!withCmd).toBeTruthy();
});
