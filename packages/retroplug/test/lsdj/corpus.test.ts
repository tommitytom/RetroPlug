// The self-sufficient decode oracle: for every embedded liblsdj content sav
// (fmt 3..11, 16 — covering EVERY version-decode branch), assert the pure-TS
// decodeSav matches the frozen C++/liblsdj-certified golden model, and that
// encode is a byte-identical inverse of the working song + a model-level inverse
// of the whole image. This replicates the (now-deletable) C++ liblsdj-differential
// coverage with zero native dependency. The exhaustive per-format losslessness
// sweep over all ~549 corpus savs is test-native/lsdj-codec-corpus.test.ts.
import { test, expect } from "../../testing/harness";
import { deepEqual } from "./_assert";
import { savBytes, FIXTURE_KEYS } from "./fixtures";
import { decodeSav, encodeSav } from "../../src/lsdj/codec/sav";
import lsdj499 from "./golden/lsdj499.json";
import lsdj620 from "./golden/lsdj620.json";
import lsdj668 from "./golden/lsdj668.json";
import lsdj671 from "./golden/lsdj671.json";
import lsdj690 from "./golden/lsdj690.json";
import all from "./golden/all.json";
import happy_birthday from "./golden/happy_birthday.json";
import lsdj732 from "./golden/lsdj732.json";
import lsdj790 from "./golden/lsdj790.json";
import lsdj798 from "./golden/lsdj798.json";
import lsdj834 from "./golden/lsdj834.json";
import lsdj888 from "./golden/lsdj888.json";

const goldens: Record<string, unknown> = {
  lsdj499, lsdj620, lsdj668, lsdj671, lsdj690, all, happy_birthday, lsdj732, lsdj790, lsdj798, lsdj834, lsdj888,
};

for (const key of FIXTURE_KEYS) {
  test(`${key}: decodeSav matches the C++/liblsdj golden model`, () => {
    deepEqual(decodeSav(savBytes(key)), goldens[key], key);
  });

  test(`${key}: encode is a byte-identical (working song) + model-level (archive) inverse`, () => {
    const sav = savBytes(key);
    const re = encodeSav(decodeSav(sav), sav);
    for (let i = 0; i < 0x8000; i++) {
      if (re[i] !== sav[i]) throw new Error(`${key}: working-song byte diff @0x${i.toString(16)}`);
    }
    deepEqual(decodeSav(re), decodeSav(sav), `${key} model round-trip`);
  });
}

test("all 12 branch-covering fixtures were exercised", () => {
  expect(FIXTURE_KEYS.length).toBe(12);
});
