// The graph document's endpoint rule, shared with C++: node ids carry no ".", so an endpoint splits at
// its first dot and a port id may itself contain dots ("pins.pulse").
import { test, expect } from "../../testing/harness";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { endpoint, parseEndpoint, toGraphJson, type GraphDocument, type NodeDoc } from "../../src/graph/graphDocument";
import golden from "../../../native/test/graph/golden/graph-document.json";

test("an endpoint splits at its first dot", () => {
  expect(parseEndpoint("gb1.pins.pulse")).toEqual({ node: "gb1", port: "pins.pulse" });
  expect(parseEndpoint("gb1")).toBe(null);
  expect(parseEndpoint(".x")).toBe(null);
  expect(parseEndpoint("x.")).toBe(null);
});

test("endpoint refuses a node id that would make the split ambiguous", () => {
  expect(endpoint("sys-3", "mix")).toBe("sys-3.mix");
  expect(() => endpoint("sys.3", "mix")).toThrow("contain no");
  expect(() => endpoint("", "mix")).toThrow("non-empty");
  expect(() => endpoint("sys 3", "mix")).toThrow("whitespace");  // the planner's node-id rule, the same plain-id rule
  expect(() => endpoint("sys\u0001", "mix")).toThrow("control characters");
  expect(() => endpoint("sys\u007f", "mix")).toThrow("control characters");
  expect(() => endpoint("sys\ud800", "mix")).toThrow("lone surrogates");
  expect(() => endpoint("sys-3", "")).toThrow("port id");  // "sys-3." would not split back
  expect(() => endpoint("sys-3", "a b")).toThrow("port id");
  expect(endpoint("sys-3/lsdj-sync", "serialIn")).toBe("sys-3/lsdj-sync.serialIn");
});

// The pact for documents, as the golden descriptors are for nodes: TS spells this document, and
// packages/native/test/graph/Registry.test.cpp parses the same golden file with the planner's parser.
const sample: GraphDocument = {
  nodes: [
    {
      id: "sys-1",
      type: "rp.sameboy.gb",
      settings: { model: "dmg", volume: 0.5, tags: ["a", "b"], rom: { path: "lsdj.gb" } },
      params: { gain: -6, mode: "midi-sync", mute: false },
    },
    { id: "sys-1/lsdj-sync", type: "rp.lsdj.sync" },
    { id: "player", type: "rp.core.buffer-player", resources: { sample: "res:12" } },
    { id: "out", type: "rp.host.audio-out", label: "Main" } as NodeDoc,  // a UI-side field C++ would refuse
  ],
  wires: [
    { from: "sys-1/lsdj-sync.serialOut", to: "sys-1.serialIn", feedback: true },
    { from: "sys-1.mix", to: "out.out.1-2" },
  ],
};

test("a document TS builds is the document C++ parses", () => {
  expect(toGraphJson(sample)).toBe(canonicalJson(golden));
  expect(toGraphJson(sample).includes("label")).toBe(false);  // only the fields C++ reads
});

test("toGraphJson refuses what the C++ parser or planner would", () => {
  const doc = (node: Record<string, unknown>, wires: unknown[] = []) => () =>
    toGraphJson({ nodes: [{ id: "n", type: "rp.x", ...node } as NodeDoc], wires: wires as never });
  expect(doc({ id: "a.b" })).toThrow("not a node id");
  expect(doc({ id: "a b" })).toThrow("not a node id");
  expect(doc({ type: "" })).toThrow("node type id");
  expect(doc({ params: { gain: Number.NaN } })).toThrow("NaN");
  expect(doc({ params: { gain: { x: 1 } } })).toThrow("number, label or bool");
  expect(doc({ settings: { tag: "\ud800" } })).toThrow("lone surrogate");
  expect(doc({ settings: [1] })).toThrow("settings must be an object");
  expect(doc({ resources: { s: "res:007" } })).toThrow("res:<id>");
  expect(doc({ resources: { s: "res:4294967296" } })).toThrow("res:<id>");
  expect(doc({}, [{ from: "n", to: "m.in" }])).toThrow("node.port");
  expect(doc({}, [{ from: "n.out", to: "m.in", feedback: 1 }])).toThrow("true or false");
});
