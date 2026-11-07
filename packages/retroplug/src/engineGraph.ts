// The engine's graph as one control-plane context publishes it (docs/modular-graph-phase-b2-design.md section
// 2.2): the model the last publish gave it (the systems with their protocol roles, the audio routing, the MIDI
// routing and the controller: docs/modular-graph-phase-b3b-design.md section 4), the host's output shape, and the
// canonical text and slots of the document that last planned. Whoever builds systems (the ProjectStore, or a DirectGraph for
// callers with no store) publishes through it, and the host shape is an edit of its own. One per native Engine: two
// publishers with separate EngineGraphs would each skip a document the other had already replaced.
//
// A refused document leaves the running plan as it was, cores, resources and tap included. The publisher keeps the
// model it published, so it is the model here too; the next document is sent even if it matches the running one, so
// the model and the engine are in step again, and a refused re-arm rides it. A send that throws counts as refused, and
// so does a withShape restore that did not plan, after which the engine still runs the temporary shape: until a
// document plans, even a shape change to the current shape is sent.
import { buildGraph, type GraphModel, type GraphSystem, type HostShape } from "./graph/buildGraph";
import { toGraphJson } from "@retroplug/core/graphDocument";
import type { Backend, GraphError, SetGraphRequest, SetGraphResult } from "./backend";

export type GraphBackend = Pick<Backend, "setGraph">;

/** What a publisher hands the graph: everything a document is built from but the host shape, which is the graph's. */
export type PublishedModel = Omit<GraphModel, "shape">;

const sameShape = (a: HostShape, b: HostShape): boolean =>
  a.kind === b.kind && (a.kind !== "multi" || b.kind !== "multi" || a.channels === b.channels);

export class EngineGraph {
  private shape_: HostShape = { kind: "multi", channels: 2 };
  private model: PublishedModel = { systems: [], routing: "stereo", midiRouting: null, controller: null };
  /** The canonical text and slots of the last document that planned; null before the first and after a refusal. */
  private sent: string | null = null;
  /** A refused document carried a re-arm: every document sent carries it until one of them plans. */
  private rearmDue = false;
  /** A document did not plan, so the engine may be running another than `shape_` and `sent` say; cleared when
   *  one plans. Not `sent === null`: a graph that never sent anything still skips its own shape. */
  private engineMayDiffer = false;
  /** A live param change moved a node away from the value the last document carried, so the engine no longer runs
   *  that document's params: set by systemsMovedLive, cleared by a plan. */
  private liveParamMoved = false;
  /** The planner's errors for the last document it refused, empty after one that planned: a caller that has a
   *  fallback for a refusal (a project load dropping the system it names) reads which node it was. */
  lastErrors: GraphError[] = [];

  constructor(private readonly backend: GraphBackend) {}

  get shape(): HostShape {
    return { ...this.shape_ };
  }

  /** Build and send the graph for this model (its systems in slot order) at the current shape. Skipped when the
   *  document and slots are unchanged, no re-arm is due and no live param edit is outstanding (`liveParamMoved`),
   *  so an edit that changes nothing sends nothing; a re-arm and a document after a live edit are sent even then,
   *  since the plan carries the tap trailer or the node no longer runs the last document's params. False when the
   *  planner refused it; the model is this one either way, so a later shape change rebuilds it and not the one
   *  running. */
  publish(model: PublishedModel, opts: { rearmTap?: boolean } = {}): boolean {
    this.model = {
      systems: model.systems.slice(),
      routing: model.routing,
      midiRouting: model.midiRouting ?? null,
      controller: model.controller ?? null,
    };
    return this.send(this.shape_, opts.rearmTap === true);
  }

  /** The model a live param edit left (the systems as the store now has them), without sending a document: a later
   *  publish, a shape change among them, then builds the store's value instead of the stale one the last document
   *  carried. The store calls this whether or not the backend took the `setParam`, so a refusal leaves the core on
   *  its old value until a document lands the store's. The running plan no longer matches that model's params, so
   *  the next publish is sent even when it rebuilds exactly the document already sent. */
  systemsMovedLive(systems: readonly GraphSystem[]): void {
    this.model = { ...this.model, systems: systems.slice() };
    this.liveParamMoved = true;
  }

  /** Rebuild the model (the last one published) at another output shape. A no-op when it is the
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
    const built = buildGraph({ ...this.model, shape });
    const document = toGraphJson(built.document);
    // The slots as well: native's slot table orders its snapshot publishes. (Two slot orders of a link group build
    // one document when nothing in it lists the systems in slot order, as the routing node does; the key holds
    // either way.)
    const key = `${document}\n${built.slots.join(",")}`;
    if (key !== this.sent || rearm || this.liveParamMoved) {
      const request: SetGraphRequest = { document, slots: built.slots };
      if (rearm) request.tap = built.tapArm ? { arm: built.tapArm } : {};
      let result: SetGraphResult;
      try {
        result = this.backend.setGraph(request);
      } catch (e) {
        this.lastErrors = [];
        this.notPlanned(rearm);
        throw e;
      }
      if (!result.ok) {
        for (const e of result.errors) {
          const at = e.nodeId ? ` at ${e.nodeId}${e.port ? "." + e.port : ""}` : "";
          console.warn(`[graph] the engine refused a document (${e.code})${at}: ${e.message}`);
        }
        this.lastErrors = result.errors;
        this.notPlanned(rearm);
        return false;
      }
      this.lastErrors = [];
      this.sent = key;
      this.rearmDue = false;
      this.engineMayDiffer = false;
      this.liveParamMoved = false;
    }
    this.shape_ = { ...shape };
    return true;
  }

  /** A document that was refused or whose send threw: the engine keeps the plan it ran, so the next one is sent even
   *  if it is the running one, carrying `rearm`. */
  private notPlanned(rearm: boolean): void {
    this.sent = null;
    this.rearmDue = rearm;
    this.engineMayDiffer = true;
    this.liveParamMoved = false;
  }
}
