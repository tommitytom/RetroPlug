// EngineGraph: what reaches setGraph, and when (docs/modular-graph-phase-b2-design.md section 2.2).
import { test, expect } from "../../testing/harness";
import { EngineGraph } from "../../src/engineGraph";
import type { SetGraphRequest, SetGraphResult } from "../../src/backend";
import type { GraphSystem } from "../../src/graph/buildGraph";

class FakeEngine {
  readonly requests: SetGraphRequest[] = [];
  refuse = false;
  setGraph(request: SetGraphRequest): SetGraphResult {
    this.requests.push(request);
    return this.refuse ? { ok: false, errors: [{ nodeId: "out", port: "", code: "bad", message: "refused" }] } : { ok: true, errors: [] };
  }
}

const gb = (id: number): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup: 0, exportMode: 0 });
const nes = (id: number): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode: 0 });
const buses = (r: SetGraphRequest): unknown =>
  (JSON.parse(r.document) as { nodes: { id: string; settings?: { buses?: number[] } }[] }).nodes.find((n) => n.id === "out")?.settings?.buses;

test("a publish sends the document and its slots; an unchanged one is skipped", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  expect(g.publish([gb(1), gb(2)], "stereo")).toBe(true);
  expect(e.requests.length).toBe(1);
  expect(e.requests[0].slots).toEqual([1, 2]);
  expect(e.requests[0].tap).toBe(undefined);
  expect(g.publish([gb(1), gb(2)], "stereo")).toBe(true);
  expect(e.requests.length).toBe(1);
  expect(g.publish([gb(2)], "stereo")).toBe(true);
  expect(e.requests.length).toBe(2);
});

test("a re-arm is sent even when the document is unchanged, with the arm the routing wants", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1)], "stereo", { rearmTap: true });
  g.publish([gb(1)], "stereo", { rearmTap: true });
  expect(e.requests.length).toBe(2);
  expect(e.requests[1].tap).toEqual({});  // a restore only
  g.publish([nes(4)], "pinSplit", { rearmTap: true });
  expect(e.requests[2].tap).toEqual({ arm: { id: 4, mode: 1 } });
  g.publish([nes(4)], "channelSplit", { rearmTap: true });
  expect(e.requests[3].tap).toEqual({ arm: { id: 4, mode: 3 } });
});

test("a refused document keeps what was there before it", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1)], "stereo");
  e.refuse = true;
  expect(g.publish([gb(1), gb(2)], "stereo")).toBe(false);
  e.refuse = false;
  // The last document that planned is still the one-system one, so publishing it again sends nothing.
  g.publish([gb(1)], "stereo");
  expect(e.requests.length).toBe(2);
  // And a shape change rebuilds the systems that planned, not the refused ones.
  g.setShape({ kind: "perSystem" });
  expect(e.requests[2].slots).toEqual([1]);
});

test("a shape change rebuilds the last published systems at the new shape", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1), gb(2)], "twoPerInstance");
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);
  expect(buses(e.requests[1])).toEqual([1, 1, 1, 1]);
  expect(e.requests[1].tap).toBe(undefined);  // a shape change never re-arms
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);  // unchanged: nothing sent
  expect(e.requests.length).toBe(2);
  expect(g.setShape({ kind: "multi", channels: 0 })).toBe(false);
  expect(g.setShape({ kind: "multi", channels: 2.5 })).toBe(false);
  expect(e.requests.length).toBe(2);
  e.refuse = true;
  expect(g.setShape({ kind: "perSystem" })).toBe(false);
  expect(g.shape).toEqual({ kind: "multi", channels: 4 });
});

test("withShape runs at a shape and puts the one before it back", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1)], "stereo");
  const inside = g.withShape({ kind: "perSystem" }, () => g.shape);
  expect(inside).toEqual({ kind: "perSystem" });
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  expect(buses(e.requests[e.requests.length - 1])).toEqual([1, 1]);
  e.refuse = true;
  let ran = false;
  expect(g.withShape({ kind: "perChannel" }, () => (ran = true))).toBe(null);
  expect(ran).toBe(false);
});
