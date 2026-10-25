// The engine's graph as one control-plane context publishes it (docs/modular-graph-phase-b2-design.md section
// 2.2): the systems and routing the last publish gave it, the host's output shape, and the canonical text of
// the document that last planned. Whoever builds systems (the ProjectStore, or a DirectGraph for callers with no
// store) publishes through it, and the host shape is an edit of its own. One per native Engine: two publishers
// with separate EngineGraphs would each skip a document the other had already replaced.
import { buildGraph, type GraphSystem, type HostShape } from "./graph/buildGraph";
import { toGraphJson } from "./graph/graphDocument";
import type { Backend, SetGraphRequest } from "./backend";
import type { AudioRouting } from "./settingsEnums";

export type GraphBackend = Pick<Backend, "setGraph">;

const sameShape = (a: HostShape, b: HostShape): boolean =>
  a.kind === b.kind && (a.kind !== "multi" || b.kind !== "multi" || a.channels === b.channels);

export class EngineGraph {
  private shape_: HostShape = { kind: "multi", channels: 2 };
  private systems: readonly GraphSystem[] = [];
  private routing: AudioRouting = "stereo";
  /** The canonical text of the last document that planned; null before the first. */
  private sent: string | null = null;

  constructor(private readonly backend: GraphBackend) {}

  get shape(): HostShape {
    return this.shape_;
  }

  /** Build and send the graph for these systems (slot order) and this routing, at the current shape. Skipped
   *  when the document is unchanged and no re-arm is due; a re-arm is sent even then, since its plan carries
   *  the tap trailer. False when the planner refused it: the engine, and this graph, keep what they had. */
  publish(systems: readonly GraphSystem[], routing: AudioRouting, opts: { rearmTap?: boolean } = {}): boolean {
    return this.send(systems, routing, this.shape_, opts.rearmTap === true);
  }

  /** Rebuild the last published systems at another output shape. A no-op when it is the current one; false,
   *  keeping the shape before it, for a document the planner refuses (more systems than a perSystem render
   *  has buses for) or a multi shape whose channels are not a positive integer, which every placement is modulo. */
  setShape(shape: HostShape): boolean {
    if (sameShape(shape, this.shape_)) return true;
    if (shape.kind === "multi" && !(Number.isInteger(shape.channels) && shape.channels >= 1)) return false;
    return this.send(this.systems, this.routing, shape, false);
  }

  /** Run `fn` at `shape`, then put the shape before it back. Null, without running `fn`, when the shape does
   *  not plan. */
  withShape<T>(shape: HostShape, fn: () => T): T | null {
    const before = this.shape_;
    if (!this.setShape(shape)) return null;
    try {
      return fn();
    } finally {
      this.setShape(before);
    }
  }

  private send(systems: readonly GraphSystem[], routing: AudioRouting, shape: HostShape, rearmTap: boolean): boolean {
    const built = buildGraph({ systems, routing, shape });
    const document = toGraphJson(built.document);
    if (document !== this.sent || rearmTap) {
      const request: SetGraphRequest = { document, slots: built.slots };
      if (rearmTap) request.tap = built.tapArm ? { arm: built.tapArm } : {};
      const result = this.backend.setGraph(request);
      if (!result.ok) {
        for (const e of result.errors)
          console.warn(`[graph] the engine refused a document (${e.code}) at ${e.nodeId}${e.port ? "." + e.port : ""}: ${e.message}`);
        return false;
      }
      this.sent = document;
    }
    this.systems = systems.slice();
    this.routing = routing;
    this.shape_ = shape;
    return true;
  }
}
