// The kit bank formats a product registers (src/kitFormats.ts, docs/modular-graph-phase-f1-design.md section 3).
// Which format an id names is a package's app contribution, so the driver resolves it through the generated list
// rather than importing a cart's assembler; here: the two ids the driver dispatches with resolve to the shell's
// formats, an id nothing registers is refused, and an id two formats name is refused rather than resolved by luck.
import { test, expect } from "@retroplug/testing";
import { KIT_FORMATS, kitFormat, resolveKitFormat } from "../../src/kitFormats";
import type { KitFormat } from "@retroplug/dsp/kitContract";
import { LSDJ_KIT_FORMAT } from "@retroplug/lsdj/kitFormat";
import { RISA_KIT_FORMAT } from "@retroplug/risa/kitFormat";

test("the driver's two ids resolve to the shell's kit formats", () => {
  expect(KIT_FORMATS.length).toBe(2);
  expect(kitFormat("lsdj") === LSDJ_KIT_FORMAT).toBe(true);
  expect(kitFormat("risa") === RISA_KIT_FORMAT).toBe(true);
});

test("an id no package registers is refused, naming it", () => {
  expect(() => kitFormat("nope")).toThrow('no package registers the kit format "nope"');
});

test("an id two formats name is refused, naming it", () => {
  const format: KitFormat<unknown> = {
    id: "risa",
    compile: async () => ({ bytes: new Uint8Array(), errors: [] }),
  };
  expect(() => resolveKitFormat("risa", [RISA_KIT_FORMAT, format])).toThrow(
    'two packages register the kit format "risa"',
  );
});
