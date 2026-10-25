// The engine's graph as one control-plane context publishes it (docs/modular-graph-phase-b2-design.md section
// 2.2): the systems and routing the last publish gave it, the host's output shape, and the canonical text and
// slots of the document that last planned. Whoever builds systems (the ProjectStore, or a DirectGraph for callers
// with no store) publishes through it, and the host shape is an edit of its own. One per native Engine: two
// publishers with separate EngineGraphs would each skip a document the other had already replaced.
//
// A refused document leaves the running plan as it was and every core it named parked until a document plans.
// The publisher keeps the systems it published either way, so they are the model here too; the next document is
// sent even if it matches the running one, so the parked cores are freed; and a refused re-arm rides it. A send
// that throws counts as refused, and so does a withShape restore that did not plan, after which the engine still
// runs the temporary shape: until a document plans, even a shape change to the current shape is sent.
import { buildGraph, type GraphSystem, type HostShape } from "./graph/buildGraph";
import { toGraphJson } from "./graph/graphDocument";
import type { Backend, SetGraphRequest, SetGraphResult } from "./backend";
import type { AudioRouting } from "./settingsEnums";

export type GraphBackend = Pick<Backend, "setGraph">;

const sameShape = (a: HostShape, b: HostShape): boolean =>
  a.kind === b.kind && (a.kind !== "multi" || b.kind !== "multi" || a.channels === b.channels);

export class EngineGraph {
  private shape_: HostShape = { kind: "multi", channels: 2 };
  private systems: readonly GraphSystem[] = [];
  private routing: AudioRouting = "stereo";
  /** The canonical text and slots of the last document that planned; null before the first and after a refusal. */
  private sent: string | null = null;
  /** A refused document carried a re-arm: every document sent carries it until one of them plans. */
  private rearmDue = false;
  /** A document did not plan, so the engine may be running another than `shape_` and `sent` say; cleared when
   *  one plans. Not `sent === null`: a graph that never sent anything still skips its own shape. */
  private engineMayDiffer = false;

  constructor(private readonly backend: GraphBackend) {}

  get shape(): HostShape {
    return { ...this.shape_ };
  }

  /** Build and send the graph for these systems (slot order) and this routing, at the current shape. Skipped
   *  when the document and slots are unchanged and no re-arm is due; a re-arm is sent even then, since its plan
   *  carries the tap trailer. False when the planner refused it; the systems and routing are the model either
   *  way, so a later shape change rebuilds them and not the ones running. */
  publish(systems: readonly GraphSystem[], routing: AudioRouting, opts: { rearmTap?: boolean } = {}): boolean {
    this.systems = systems.slice();
    this.routing = routing;
    return this.send(this.shape_, opts.rearmTap === true);
  }

  /** Rebuild the model (the last published systems and routing) at another output shape. A no-op when it is the
   *  current one, unless a document that did not plan left the engine's own unknown; false, keeping the shape
   *  before it, for a document the planner refuses (more systems than a perSystem render has buses for) or a
   *  multi shape whose channels are not a positive integer, which every placement is modulo. It re-arms the tap
   *  only when a refused re-arm is due. */
  setShape(shape: HostShape): boolean {
    if (sameShape(shape, this.shape_) && !this.engineMayDiffer) return true;
    if (shape.kind === "multi" && !(Number.isInteger(shape.channels) && shape.channels >= 1)) return false;
    return this.send(shape, false);
  }

  /** Run `fn` at `shape`, then put the shape before it back, whatever `fn` did. Null, without running `fn`, when
   *  the shape does not plan. */
  withShape<T>(shape: HostShape, fn: () => T): T | null {
    const before = this.shape_;
    if (!this.setShape(shape)) return null;
    let returned = false;
    try {
      const value = fn();
      returned = true;
      return value;
    } finally {
      this.restore(before, returned);
    }
  }

  /** Put withShape's shape back. One that does not plan (refused, or the send threw) is the shape anyway, with no
   *  document counted as sent and the engine left running the temporary shape, so the next publish or shape
   *  change sends at it. Its throw is rethrown only when `fn` returned: it never replaces an error of `fn`'s. */
  private restore(before: HostShape, rethrow: boolean): void {
    let planned = false;
    try {
      planned = this.setShape(before);
    } catch (e) {
      if (rethrow) throw e;
      console.warn(`[graph] putting the shape back threw: ${String(e)}`);
    } finally {
      if (!planned) {
        this.shape_ = before;
        this.sent = null;
        this.engineMayDiffer = true;
      }
    }
  }

  private send(shape: HostShape, rearmTap: boolean): boolean {
    const rearm = rearmTap || this.rearmDue;
    const built = buildGraph({ systems: this.systems, routing: this.routing, shape });
    const document = toGraphJson(built.document);
    // The slots as well: two slot orders can build one document (a link group sums after the unlinked systems),
    // and native's slot table orders its outputs and snapshot publishes.
    const key = `${document}\n${built.slots.join(",")}`;
    if (key !== this.sent || rearm) {
      const request: SetGraphRequest = { document, slots: built.slots };
      if (rearm) request.tap = built.tapArm ? { arm: built.tapArm } : {};
      let result: SetGraphResult;
      try {
        result = this.backend.setGraph(request);
      } catch (e) {
        this.notPlanned(rearm);
        throw e;
      }
      if (!result.ok) {
        for (const e of result.errors) {
          const at = e.nodeId ? ` at ${e.nodeId}${e.port ? "." + e.port : ""}` : "";
          console.warn(`[graph] the engine refused a document (${e.code})${at}: ${e.message}`);
        }
        this.notPlanned(rearm);
        return false;
      }
      this.sent = key;
      this.rearmDue = false;
      this.engineMayDiffer = false;
    }
    this.shape_ = { ...shape };
    return true;
  }

  /** A document that was refused or whose send threw: its cores stay parked until a document plans, so the next
   *  one is sent even if it is the running one, carrying `rearm`. */
  private notPlanned(rearm: boolean): void {
    this.sent = null;
    this.rearmDue = rearm;
    this.engineMayDiffer = true;
  }
}
