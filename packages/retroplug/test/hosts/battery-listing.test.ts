// A battery listing's lines (docs/modular-graph-phase-f4b-design.md section 5.2), recorded from 8a74339b9's `n8-load`
// `decodeAndPrint` before a cart's rows moved into its tracker integration: the header, one row per saved song and the
// refusal line are byte-identical, so `n8-load --show-song`'s output did not move with the code that composes it.
import { test, expect } from "@retroplug/testing";
import { savBytes as risaSav } from "@retroplug/risa/testFixtures";
import { savBytes as lsdjSav } from "@retroplug/lsdj/testFixtures";
import { batteryListing } from "../../src/batteryListing";

test("a risa battery lists its songs as the old listing did", () => {
  // Recorded from the old code: the header names the cart, and each row is the slot, the name, the record version and
  // its length.
  expect(batteryListing(risaSav("v2_blumarbl"))).toEqual([
    "risa battery - 1 song(s):",
    "  [ 0] BLUMARBL  v5  6905B",
  ]);
});

test("an LSDj battery lists its projects as the old listing did", () => {
  expect(batteryListing(lsdjSav("lsdj888"))).toEqual(["LSDj battery - 1 project(s):", "  [ 0] SNDTESTx"]);
});

test("an image no cart reads gets the one refusal line naming them in rank order", () => {
  expect(batteryListing(new Uint8Array(0x8000))).toEqual([
    "unrecognized battery image (32768 bytes) - not a risa or LSDj save",
  ]);
});
