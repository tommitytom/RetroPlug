// Phase 1 gate: the zod SSOT (model.ts) reproduces the reflect-cpp JSON contract.
// - Sav.parse({}) == the C++ default image (frozen golden empty.json).
// - fixed(elem,N): short/omitted pads to N with the element default; > N throws.
// - bounded sub-byte fields REJECT out-of-range (not silently clamp/truncate).
// - optional struct fields (table/length) omit; optional array cells are null.
import { test, expect } from "../../testing/harness";
import { deepEqual } from "./_assert";
import {
  SavSchema,
  SongSchema,
  PhraseSchema,
  GrooveSchema,
  InstrumentSchema,
  fixed,
} from "../../src/lsdj/model";
import { z } from "zod";
import emptyGolden from "./golden/empty.json";

test("Sav.parse({}) equals the C++ default image (empty golden)", () => {
  deepEqual(SavSchema.parse({}), emptyGolden, "sav");
});

test("Song.parse({}) is a full default working song (fmt22, all fixed arrays full length)", () => {
  const s = SongSchema.parse({});
  expect(s.formatVersion).toBe(22);
  expect(s.rows.length).toBe(256);
  expect(s.chains.length).toBe(128);
  expect(s.phrases.length).toBe(256);
  expect(s.instruments.length).toBe(64);
  expect(s.tables.length).toBe(32);
  expect(s.grooves.length).toBe(32);
  expect(s.synths.length).toBe(16);
  expect(s.waves.length).toBe(256);
  expect(s.words.length).toBe(0x540);
  // load-bearing defaults
  expect(s.settings.tempo).toBe(128);
  expect(s.settings.syncMode).toBe("None");
  expect(s.grooves[0].steps.length).toBe(16);
  expect(s.grooves[0].steps[0]).toBe(6);
  expect(s.grooves[0].steps[1]).toBe(6);
  expect(s.grooves[0].steps[2]).toBe(0);
  // unallocated cells are null
  expect(s.chains[0]).toBe(null);
  expect(s.instruments[0]).toBe(null);
  expect(s.rows[0].chains[0]).toBe(null);
});

test("fixed() pads a short array to N with element defaults", () => {
  const p = PhraseSchema.parse({ notes: [1, 2, 3] });
  expect(p.notes.length).toBe(16);
  expect(p.notes[0]).toBe(1);
  expect(p.notes[2]).toBe(3);
  expect(p.notes[3]).toBe(0); // padded default
  expect(p.instruments.length).toBe(16);
  expect(p.instruments[0]).toBe(null); // nullable cell default
  expect(p.commands[0]).toBe("None");
});

test("fixed() rejects an array longer than N", () => {
  const tooMany = Array.from({ length: 17 }, (_, i) => i);
  expect(() => PhraseSchema.parse({ notes: tooMany })).toThrow();
  const s = fixed(z.number().int().default(0), 4);
  expect(() => s.parse([1, 2, 3, 4, 5])).toThrow();
});

test("groove default is the factory 6/6, padded to 16", () => {
  const g = GrooveSchema.parse({});
  expect(g.steps.length).toBe(16);
  expect(g.steps[0]).toBe(6);
  expect(g.steps[1]).toBe(6);
  expect(g.steps[15]).toBe(0);
});

test("bounded sub-byte fields reject out-of-range (no silent truncation)", () => {
  // finetune is a Nibble (0..15)
  expect(() => InstrumentSchema.parse({ type: "pulse", finetune: 16 })).toThrow();
  // kit1 is a U5 (0..31)
  expect(() => InstrumentSchema.parse({ type: "kit", kit1: 32 })).toThrow();
  // a byte field rejects 256
  expect(() => InstrumentSchema.parse({ type: "pulse", sweep: 256 })).toThrow();
  // in range is fine
  expect(InstrumentSchema.parse({ type: "pulse", finetune: 15 }).type).toBe("pulse");
});

test("optional struct fields omit; a set table serializes inline", () => {
  const noTable = InstrumentSchema.parse({ type: "pulse" });
  expect("table" in noTable).toBeFalsy(); // omitted, not null
  expect("length" in noTable).toBeFalsy();
  const withTable = InstrumentSchema.parse({ type: "pulse", table: 3 }) as { table?: number };
  expect(withTable.table).toBe(3);
});

test("enum values are the C++ name strings (incl sparse SyncMode by name)", () => {
  const s = SongSchema.parse({ settings: { syncMode: "MidiMap" } });
  expect(s.settings.syncMode).toBe("MidiMap");
  expect(() => SongSchema.parse({ settings: { syncMode: "Nope" } })).toThrow();
});
