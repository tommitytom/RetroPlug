// EngineGraph: what reaches setGraph, and when (docs/modular-graph-phase-b2-design.md section 2.2).
import { test, expect } from "../../testing/harness";
import { EngineGraph } from "../../src/engineGraph";
import { buildGraph, type GraphSystem } from "../../src/graph/buildGraph";
import { toGraphJson } from "../../src/graph/graphDocument";
import type { SetGraphRequest, SetGraphResult } from "../../src/backend";

class FakeEngine {
  readonly requests: SetGraphRequest[] = [];
  refuse = false;
  /** Thrown by setGraph once it has recorded the request, as a backend call that fails would. */
  fail: Error | null = null;
  setGraph(request: SetGraphRequest): SetGraphResult {
    this.requests.push(request);
    if (this.fail) throw this.fail;
    return this.refuse ? { ok: false, errors: [{ nodeId: "out", port: "", code: "bad", message: "refused" }] } : { ok: true, errors: [] };
  }
}

const gb = (id: number, linkGroup = 0): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup, exportMode: 0, pipeline: [] });
const nes = (id: number): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode: 0, pipeline: [] });
const buses = (r: SetGraphRequest): unknown =>
  (JSON.parse(r.document) as { nodes: { id: string; settings?: { buses?: number[] } }[] }).nodes.find((n) => n.id === "out")?.settings?.buses;

test("a publish sends the document and its slots; an unchanged one is skipped", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  expect(g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(1);
  expect(e.requests[0].slots).toEqual([1, 2]);
  expect(e.requests[0].tap).toBe(undefined);
  expect(g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(1);
  expect(g.publish({ systems: [gb(2)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(2);
});

test("another slot order is sent, with its slots", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  // A link group sums after the unlinked systems, so both orders wire the same audio; the kernel lists its systems
  // in slot order (positional MIDI routing reads it), and the slots order the engine's snapshot publishes.
  g.publish({ systems: [gb(1), gb(2, 5), gb(3, 5)], routing: "stereo", project: [] });
  g.publish({ systems: [gb(2, 5), gb(1), gb(3, 5)], routing: "stereo", project: [] });
  expect(e.requests.length).toBe(2);
  const kernelIds = (r: SetGraphRequest) =>
    (JSON.parse(r.document) as { nodes: { id: string; settings?: { systems?: { id: number }[] } }[] }).nodes
      .find((n) => n.id === "kernel")
      ?.settings?.systems?.map((s) => s.id);
  expect(kernelIds(e.requests[1])).toEqual([2, 1, 3]);
  expect(e.requests[1].slots).toEqual([2, 1, 3]);
});

test("the kernel's structure is part of the document: a pipeline or project-stage edit sends one, the same model none", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  const mgb = { kind: "mgb", config: {} };
  const routing = { kind: "midi-routing", config: { mode: "sendToAll" } };
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  g.publish({ systems: [{ ...gb(1), pipeline: [mgb] }], routing: "stereo", project: [] });
  g.publish({ systems: [{ ...gb(1), pipeline: [mgb] }], routing: "stereo", project: [routing] });
  g.publish({ systems: [{ ...gb(1), pipeline: [{ ...mgb }] }], routing: "stereo", project: [{ ...routing }] });
  expect(e.requests.length).toBe(3);
  expect(e.requests.every((r) => r.tap === undefined)).toBe(true);
});

test("a re-arm is sent even when the document is unchanged, with the arm the routing wants", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] }, { rearmTap: true });
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] }, { rearmTap: true });
  expect(e.requests.length).toBe(2);
  expect(e.requests[1].tap).toEqual({});  // a restore only
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] }, { rearmTap: true });
  expect(e.requests[2].tap).toEqual({ arm: { id: 4, mode: 1 } });
  g.publish({ systems: [nes(4)], routing: "channelSplit", project: [] }, { rearmTap: true });
  expect(e.requests[3].tap).toEqual({ arm: { id: 4, mode: 3 } });
});

test("after a refused publish the same model is sent again", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  e.refuse = true;
  expect(g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] })).toBe(false);
  e.refuse = false;
  expect(g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(3);
  expect(e.requests[2].slots).toEqual([1, 2]);
});

test("after a refused publish the document running before it is sent, which frees the cores it parked", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  e.refuse = true;
  g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] });
  e.refuse = false;
  expect(g.publish({ systems: [gb(1)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(3);
  expect(e.requests[2].document).toBe(e.requests[0].document);
  // Once that planned, it is skipped again.
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  expect(e.requests.length).toBe(3);
});

test("a shape change after a refused publish rebuilds the refused systems and routing at the new shape", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  e.refuse = true;
  g.publish({ systems: [gb(1), gb(2)], routing: "twoPerInstance", project: [] });
  e.refuse = false;
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);
  const shape = { kind: "multi", channels: 4 } as const;
  const refused = buildGraph({ systems: [gb(1), gb(2)], routing: "twoPerInstance", shape, project: [] });
  expect(e.requests[2].document).toBe(toGraphJson(refused.document));
  expect(e.requests[2].slots).toEqual([1, 2]);
});

test("a refused re-arm rides the next publish until one plans, and then an unchanged one sends nothing", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [nes(4)], routing: "stereo", project: [] });
  e.refuse = true;
  expect(g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] }, { rearmTap: true })).toBe(false);
  // A publish with no re-arm of its own carries it, and a refusal of that keeps it due.
  expect(g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] })).toBe(false);
  expect(e.requests[2].tap).toEqual({ arm: { id: 4, mode: 1 } });
  e.refuse = false;
  expect(g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] })).toBe(true);
  expect(e.requests[3].tap).toEqual({ arm: { id: 4, mode: 1 } });
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] });
  expect(e.requests.length).toBe(4);
});

test("a refused re-arm rides the next shape change, and then an unchanged publish sends nothing", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] });
  e.refuse = true;
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] }, { rearmTap: true });
  e.refuse = false;
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);
  expect(e.requests[2].tap).toEqual({ arm: { id: 4, mode: 1 } });
  expect(buses(e.requests[2])).toEqual([1, 1, 1, 1]);
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] });
  expect(e.requests.length).toBe(3);
});

test("a shape change does not re-arm a lone NES's tap when no re-arm is due", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] });
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);
  expect(e.requests.length).toBe(2);
  expect(e.requests[1].tap).toBe(undefined);
});

test("a shape change rebuilds the last published systems at the stored routing and the new shape", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  const systems = [gb(1), gb(2)];
  g.publish({ systems, routing: "twoPerInstance", project: [] });
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);
  const shape = { kind: "multi", channels: 4 } as const;
  expect(e.requests[1].document).toBe(toGraphJson(buildGraph({ systems, routing: "twoPerInstance", shape, project: [] }).document));
  expect(e.requests[1].tap).toBe(undefined);  // a shape change never re-arms
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);  // unchanged: nothing sent
  expect(e.requests.length).toBe(2);
  expect(g.setShape({ kind: "multi", channels: 0 })).toBe(false);
  expect(g.setShape({ kind: "multi", channels: 2.5 })).toBe(false);
  expect(e.requests.length).toBe(2);
});

test("a refused shape change keeps the shape before it, and the next publish goes out at that shape", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] });
  g.setShape({ kind: "multi", channels: 4 });
  e.refuse = true;
  expect(g.setShape({ kind: "perSystem" })).toBe(false);
  expect(g.shape).toEqual({ kind: "multi", channels: 4 });
  e.refuse = false;
  expect(g.publish({ systems: [gb(1), gb(2)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(4);
  expect(buses(e.requests[3])).toEqual([1, 1, 1, 1]);
});

test("the shape is kept and handed out as a copy", () => {
  const g = new EngineGraph(new FakeEngine());
  const shape = { kind: "multi" as const, channels: 4 };
  g.setShape(shape);
  shape.channels = 6;
  expect(g.shape).toEqual({ kind: "multi", channels: 4 });
  const out = g.shape;
  if (out.kind === "multi") out.channels = 8;
  expect(g.shape).toEqual({ kind: "multi", channels: 4 });
});

test("withShape runs at a shape and puts the one before it back", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  const inside = g.withShape({ kind: "perSystem" }, () => g.shape);
  expect(inside).toEqual({ kind: "perSystem" });
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  expect(buses(e.requests[e.requests.length - 1])).toEqual([1, 1]);
  e.refuse = true;
  let ran = false;
  expect(g.withShape({ kind: "perChannel" }, () => (ran = true))).toBe(null);
  expect(ran).toBe(false);
});

test("withShape puts the shape before it back when fn throws, and fn's error is what reaches the caller", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  expect(() => g.withShape({ kind: "perSystem" }, () => {
    throw new Error("from fn");
  })).toThrow("from fn");
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  expect(e.requests.length).toBe(3);
  expect(e.requests[2].document).toBe(e.requests[0].document);
  // A restore that throws too does not replace it.
  expect(() => g.withShape({ kind: "perSystem" }, () => {
    e.fail = new Error("from the engine");
    throw new Error("from fn");
  })).toThrow("from fn");
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
});

test("a restore that does not plan still puts the shape before it back, and the next publish is sent at it", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
  expect(g.withShape({ kind: "perSystem" }, () => ((e.refuse = true), 7))).toBe(7);
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  e.refuse = false;
  expect(g.publish({ systems: [gb(1)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(4);
  expect(buses(e.requests[3])).toEqual([1, 1]);
  // A restore whose send throws is the same, and its error reaches the caller when fn returned.
  const engineDown = () => ((e.fail = new Error("from the engine")), 7);
  expect(() => g.withShape({ kind: "perSystem" }, engineDown)).toThrow("from the engine");
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  e.fail = null;
  expect(g.publish({ systems: [gb(1)], routing: "stereo", project: [] })).toBe(true);
  expect(e.requests.length).toBe(7);
  expect(buses(e.requests[6])).toEqual([1, 1]);
});

test("after a restore that did not plan, withShape or setShape at the shape before sends it, until one plans", () => {
  const stereo = { kind: "multi", channels: 2 } as const;
  // Three requests, and the engine still runs the perSystem plan the refused restore was to replace.
  const refusedRestore = (): [FakeEngine, EngineGraph] => {
    const e = new FakeEngine();
    const g = new EngineGraph(e);
    g.publish({ systems: [gb(1)], routing: "stereo", project: [] });
    g.withShape({ kind: "perSystem" }, () => (e.refuse = true));
    e.refuse = false;
    return [e, g];
  };
  const [e1, g1] = refusedRestore();
  expect(g1.withShape(stereo, () => e1.requests.length)).toBe(4);
  expect(buses(e1.requests[3])).toEqual([1, 1]);
  expect(g1.setShape(stereo)).toBe(true);
  expect(e1.requests.length).toBe(4);
  const [e2, g2] = refusedRestore();
  expect(g2.setShape(stereo)).toBe(true);
  expect(e2.requests.length).toBe(4);
  expect(buses(e2.requests[3])).toEqual([1, 1]);
  expect(g2.withShape(stereo, () => e2.requests.length)).toBe(4);
  expect(e2.requests.length).toBe(4);
});

test("a graph that has sent nothing skips a shape change to its own shape", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  expect(g.setShape({ kind: "multi", channels: 2 })).toBe(true);
  expect(g.withShape({ kind: "multi", channels: 2 }, () => 1)).toBe(1);
  expect(e.requests.length).toBe(0);
});

test("a send that throws counts as refused, and its error reaches the caller", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] });
  e.fail = new Error("from the engine");
  expect(() => g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] }, { rearmTap: true })).toThrow("from the engine");
  e.fail = null;
  // The same model again is sent, with the re-arm the throwing one asked for.
  expect(g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] })).toBe(true);
  expect(e.requests.length).toBe(3);
  expect(e.requests[2].tap).toEqual({ arm: { id: 4, mode: 1 } });
  // A throwing publish of other systems leaves the running document to be sent again.
  e.fail = new Error("from the engine");
  expect(() => g.publish({ systems: [nes(4), nes(5)], routing: "pinSplit", project: [] })).toThrow("from the engine");
  e.fail = null;
  expect(g.publish({ systems: [nes(4)], routing: "pinSplit", project: [] })).toBe(true);
  expect(e.requests.length).toBe(5);
  expect(e.requests[4].document).toBe(e.requests[2].document);
  // A throwing shape change keeps the shape, and the engine's is unknown until a document plans.
  e.fail = new Error("from the engine");
  expect(() => g.setShape({ kind: "perSystem" })).toThrow("from the engine");
  e.fail = null;
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  expect(g.setShape({ kind: "multi", channels: 2 })).toBe(true);
  expect(e.requests.length).toBe(7);
  expect(e.requests[6].document).toBe(e.requests[2].document);
});
