// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down) and publishes after each
// call. Use one per process, directGraph(): two publishers with separate EngineGraphs would each drop the other's
// systems from the document. Sharing a session's EngineGraph (so the session's render calls render what this
// places) does not merge the two models either: each publish replaces the graph's model with the publisher's own
// systems. It is safe only while the session's stores place nothing, the two publishing in turn, as
// test-native/parity/render-blocks.test.ts and cli/sessions/analyze-lsdj-sync.ts do.
import { createRealBackend } from "../realBackend";
import type { AudioRouting } from "../settingsEnums";
import type { ConstructSpec, EmulatorBackend } from "../backend";
import { EngineGraph } from "../engineGraph";
import type { GraphSystem } from "./buildGraph";

export type DirectBackend = Pick<EmulatorBackend, "buildCore" | "setGraph" | "applyRoleConfig">;

export class DirectGraph {
  readonly graph: EngineGraph;
  private systems: GraphSystem[] = [];
  private routing: AudioRouting = "stereo";

  constructor(
    private readonly backend: DirectBackend,
    graph?: EngineGraph,
  ) {
    this.graph = graph ?? new EngineGraph(backend);
  }

  /** The systems this graph places, in slot order. */
  ids(): number[] {
    return this.systems.map((s) => s.id);
  }

  /** Build a core under `id` and append it. False when it did not build, or the graph did not plan (the core
   *  is then freed by the next document that plans). */
  add(spec: ConstructSpec, id: number): boolean {
    const built = this.backend.buildCore(spec, id);
    if (!built) return false;
    return this.change(() => this.systems.push({ id, ...built }), true);
  }

  /** Build a core under `id` in `oldId`'s slot. False when `oldId` is absent (the tap is re-armed all the same,
   *  as a replace of a gone system always did), or when the core did not build. */
  replace(oldId: number, spec: ConstructSpec, id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === oldId);
    if (at < 0) {
      this.publish(true);
      return false;
    }
    const built = this.backend.buildCore(spec, id);
    if (!built) return false;
    return this.change(() => (this.systems[at] = { id, ...built }), true);
  }

  /** Drop system `id`; later slots shift down. An id already gone re-arms the tap and changes nothing else. */
  remove(id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return this.publish(true);
    return this.change(() => this.systems.splice(at, 1), true);
  }

  setRouting(routing: AudioRouting): boolean {
    return this.change(() => (this.routing = routing), true);
  }

  /** Apply a system role's config to the live core. A `sameboy` config's link group is wiring as well: native
   *  decodes a config without one as group 0, so the cable follows what the core is told. */
  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
    const ok = this.backend.applyRoleConfig(id, kind, config);
    const at = this.systems.findIndex((s) => s.id === id);
    if (kind !== "sameboy" || at < 0 || this.systems[at].type !== "rp.sameboy.gb") return ok;
    const group = typeof config.linkGroupId === "number" ? config.linkGroupId : 0;
    if (group !== this.systems[at].linkGroup) this.change(() => (this.systems[at] = { ...this.systems[at], linkGroup: group }), false);
    return ok;
  }

  // Apply an edit to the model and publish; a refused document puts the model back.
  private change(edit: () => unknown, rearmTap: boolean): boolean {
    const systems = this.systems.slice();
    const routing = this.routing;
    edit();
    if (this.publish(rearmTap)) return true;
    this.systems = systems;
    this.routing = routing;
    // EngineGraph keeps a refused document's systems as its model, so hand it the restored one: otherwise a later
    // shape change would place the systems of the edit just rolled back.
    this.publish(false);
    return false;
  }

  private publish(rearmTap: boolean): boolean {
    return this.graph.publish(this.systems, this.routing, { rearmTap });
  }
}

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
