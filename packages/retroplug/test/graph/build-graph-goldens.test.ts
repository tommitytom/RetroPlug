// buildGraph against the B1 native translator, case for case: golden/build-graph.json is what the translator
// built for every model in a matrix of system sets, routings and host shapes (written by
// packages/native/test/engine-graph/TranslatorGoldens.test.cpp while it existed). Node order, wire order and
// settings must all agree: document order is plan order on ties, and wire order is each add's input order.
// Once the translator is deleted, buildGraph is the record and nothing generates golden/build-graph.json: an
// intentional change to the document it writes rewrites the file's documents by hand (a short script mapping
// each case's model through buildGraph), and says why in its commit. B3a did so for the DSP kernel and the host
// I/O around the audio (every model gained an empty kernel structure; the audio of every document is the
// translator's still). Not every golden document plans: the thirty-three systems under perSystem give
// rp.host.audio-out 33 buses, past the 32 it takes.
// packages/native/test/engine-graph/BuildGraphGoldens.test.cpp plans every other one on the engine.
import { test, expect } from "../../testing/harness";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { buildGraph, type GraphModel, type TapArm } from "../../src/graph/buildGraph";
import golden from "./golden/build-graph.json";

interface GoldenCase {
  name: string;
  model: GraphModel;
  document: number;
  slots: number[];
  tapArm: TapArm | null;
}

const goldens = golden as unknown as { documents: unknown[]; cases: GoldenCase[] };

test("the goldens cover the matrix", () => {
  expect(goldens.cases.length).toBe(840);
});

test("buildGraph builds the translator's document, slots and tap for every golden model", () => {
  const wrong: string[] = [];
  for (const c of goldens.cases) {
    const built = buildGraph(c.model);
    if (canonicalJson(built.document) !== canonicalJson(goldens.documents[c.document])) wrong.push(`${c.name}: document`);
    if (canonicalJson(built.slots) !== canonicalJson(c.slots)) wrong.push(`${c.name}: slots`);
    if (canonicalJson(built.tapArm) !== canonicalJson(c.tapArm)) wrong.push(`${c.name}: tapArm`);
  }
  expect(wrong).toEqual([]);
});
