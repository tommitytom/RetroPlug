// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down) and publishes after each
// call. Use one per process, directGraph(), or one sharing a session's EngineGraph: two publishers with separate
// models would each drop the other's systems from the document.
import { createRealBackend } from "../realBackend";
import { audioRoutingToIndex, type AudioRouting } from "../settingsEnums";
import type { ConstructSpec, EmulatorBackend } from "../backend";
import { EngineGraph } from "../engineGraph";

export type DirectBackend = Pick<
  EmulatorBackend,
  "constructSystem" | "removeSystem" | "setAudioRouting" | "applyRoleConfig" | "buildCore" | "setGraph"
>;

export class DirectGraph {
  readonly graph: EngineGraph;

  constructor(
    private readonly backend: DirectBackend,
    graph?: EngineGraph,
  ) {
    this.graph = graph ?? new EngineGraph(backend);
  }

  /** Build a core under `id` and append it. False when it did not build. */
  add(spec: ConstructSpec, id: number): boolean {
    return this.backend.constructSystem(spec, id);
  }

  /** Build a core under `id` in `oldId`'s slot. False when it did not build or `oldId` is absent. */
  replace(oldId: number, spec: ConstructSpec, id: number): boolean {
    return this.backend.constructSystem({ ...spec, replaceId: oldId }, id);
  }

  /** Drop system `id`; later slots shift down. */
  remove(id: number): boolean {
    return this.backend.removeSystem(id);
  }

  setRouting(routing: AudioRouting): boolean {
    return this.backend.setAudioRouting(audioRoutingToIndex(routing));
  }

  /** Apply a system role's config to the live core. A `sameboy` config's link group is wiring as well. */
  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
    return this.backend.applyRoleConfig(id, kind, config);
  }
}

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
