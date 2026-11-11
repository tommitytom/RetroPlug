// The `midi` project section's parse and merge (docs/modular-graph-phase-f5b-design.md section 6.2): the entries are
// objects with room for a routing field (decision 7), so every field an entry carries beside `name` survives a load and
// a write - that additive field is what needs no version bump.
import { test, expect } from "@retroplug/testing";
import { CLOCK_BPM_MAX, CLOCK_BPM_MIN, DEFAULT_CLOCK_BPM, midiSection } from "../configSection";

const parse = (raw: unknown) => midiSection.schema.parse(raw) as {
  inputs: { name: string; [field: string]: unknown }[];
  allInputs: boolean;
  outputs: { name: string; [field: string]: unknown }[];
  clockBpm: number;
};

test("an entry keeps every field it carried beside its name", () => {
  const section = parse({ inputs: [{ name: "LP", route: 3, split: { low: 36 } }] });
  expect(section.inputs).toEqual([{ name: "LP", route: 3, split: { low: 36 } }]);
});

test("a bare name a first draft of the shape used reads as its object", () => {
  expect(parse({ outputs: ["Deluge"] }).outputs).toEqual([{ name: "Deluge" }]);
});

test("repeated and empty names collapse to the first entry, fields and all", () => {
  const section = parse({
    inputs: [{ name: "LP", route: 1 }, { name: "LP", route: 2 }, { name: "" }, 7, { nothing: true }],
  });
  expect(section.inputs).toEqual([{ name: "LP", route: 1 }]);
});

test("the tempo is clamped to the range the estimator accepts, and a missing one is the default", () => {
  expect(parse({ clockBpm: 5 }).clockBpm).toBe(CLOCK_BPM_MIN);
  expect(parse({ clockBpm: 5000 }).clockBpm).toBe(CLOCK_BPM_MAX);
  expect(parse({ clockBpm: "fast" }).clockBpm).toBe(DEFAULT_CLOCK_BPM);
  expect(parse({}).clockBpm).toBe(DEFAULT_CLOCK_BPM);
});

test("a merge keeps each entry's fields when a row writes one list or the tempo", () => {
  const current = parse({ inputs: [{ name: "LP", route: 3 }], outputs: [{ name: "Deluge", route: 9 }], clockBpm: 120 });
  const next = midiSection.merge!(current, { clockBpm: 130 }) as ReturnType<typeof parse>;
  expect(next.clockBpm).toBe(130);
  expect(next.inputs).toEqual([{ name: "LP", route: 3 }]);
  expect(next.outputs).toEqual([{ name: "Deluge", route: 9 }]);
});
