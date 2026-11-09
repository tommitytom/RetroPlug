// Two spellings of one fact, pinned (docs/modular-graph-phase-f4b-design.md section 4.3). "Drive Real Cart" is a
// declarative row over the mesen role's `driveRealCart`, and the NES console descriptor names the same field as its
// real-cart opt-out (`realCartOptOut`), which buildGraph reads to decide which system's traffic reaches a physical cart.
// A rename on either side would leave the row editing a key nothing places by, and nothing else would notice.
import { test, expect } from "@retroplug/testing";
import { MESEN_ROWS } from "../ui";
import { MESEN_ROLE } from "../app";
import consoles from "../consoles";

test("the Drive Real Cart row edits the field the NES descriptor names as its real-cart opt-out", () => {
  const nes = consoles.consoles?.find((descriptor) => descriptor.platforms.some((p) => p.id === "nes"));
  expect(nes !== undefined).toBe(true);
  const optOut = nes!.platforms.find((p) => p.id === "nes")!.realCartOptOut;
  const row = MESEN_ROWS.find((r) => r.id === "sys-nes-drive-cart");
  expect(row !== undefined).toBe(true);
  expect(row!.key).toBe(optOut);
  // The row is a toggle over the role's own field, and its default is the schema's (ON: every project drove a cart
  // before the setting existed).
  expect("toggle" in row!).toBe(true);
  expect(MESEN_ROLE.schema.parse({})[row!.key]).toBe(true);
});

test("the NES rows are gated on the platform, and every row edits a key of the mesen schema", () => {
  const schema = MESEN_ROLE.schema.parse({});
  for (const row of MESEN_ROWS) {
    expect(Object.prototype.hasOwnProperty.call(schema, row.key)).toBe(true);
    expect(row.role).toBe("mesen");
  }
  const nesRows = MESEN_ROWS.filter((row) => row.platforms?.includes("nes")).map((row) => row.id);
  expect(nesRows).toEqual([
    "sys-nes-region",
    "sys-nes-spritelimit",
    "sys-nes-apu-latency",
    "sys-nes-exp-volume",
    "sys-nes-drive-cart",
  ]);
  expect(MESEN_ROWS.find((row) => row.id === "sys-sms-fm")!.platforms).toEqual(["sms", "gg"]);
});
