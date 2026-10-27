// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down), each system's roles with a
// protocol node (docs/modular-graph-phase-b3b-design.md section 4), none until set, and the project's MIDI routing
// and controller, none until set (so no MIDI is routed to any system). It publishes after each call. Use one per
// process, directGraph(): two publishers with separate EngineGraphs would each drop the other's systems from the
// document. Sharing a session's EngineGraph (so the session's render calls render what this places) does not merge
// the two models either: each publish replaces the graph's model with the publisher's own systems. It is safe only
// while the session's stores place nothing, the two publishing in turn, as test-native/parity/render-blocks.test.ts
// and cli/sessions/analyze-lsdj-sync.ts do.
import { createRealBackend } from "../realBackend";
import type { AudioRouting, ControllerTarget, MidiRouting } from "../settingsEnums";
import type { RoleInstance } from "../systemRoles";
import type { ConstructSpec, EmulatorBackend } from "../backend";
import type { RowTicksTable } from "../lsdj/playback/predict";
import { EngineGraph } from "../engineGraph";
import { parseAppConfig } from "../controller/registry";
import type { GraphSystem } from "./buildGraph";
import type { GraphController } from "./controller";
import { protocolPipeline } from "./protocolNodes";

export type DirectBackend = Pick<EmulatorBackend, "buildCore" | "setGraph" | "applyRoleConfig">;

export class DirectGraph {
  readonly graph: EngineGraph;
  private systems: GraphSystem[] = [];
  private routing: AudioRouting = "stereo";
  private midiRouting: MidiRouting | null = null;
  private controller: GraphController | null = null;

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

  /** Build a core under `id` and append it, with no roles. False when it did not build, or the graph did not plan
   *  (the core is then freed by the next document that plans). */
  add(spec: ConstructSpec, id: number): boolean {
    const built = this.backend.buildCore(spec, id);
    if (!built) return false;
    return this.change(() => this.systems.push({ id, ...built, pipeline: [] }), true);
  }

  /** Build a core under `id` in `oldId`'s slot, with no roles (a new system: the protocol nodes of `oldId` go with
   *  it). False when `oldId` is absent (the tap is re-armed all the same, as a replace of a gone system always
   *  did), or when the core did not build. */
  replace(oldId: number, spec: ConstructSpec, id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === oldId);
    if (at < 0) {
      this.publish(true);
      return false;
    }
    const built = this.backend.buildCore(spec, id);
    if (!built) return false;
    return this.change(() => (this.systems[at] = { id, ...built, pipeline: [] }), true);
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

  /** System `id`'s roles, in order: each that has a protocol node is placed as one, with its config as given as
   *  the node's settings (a role kept from the last call keeps its node and state; a role with no node is left
   *  out). False when `id` is not placed here, or the document did not plan. */
  setPipeline(id: number, roles: RoleInstance[]): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return false;
    return this.change(() => (this.systems[at] = { ...this.systems[at], pipeline: protocolPipeline(roles) }), false);
  }

  /** The project's stages as the DSP kernel took them, onto the graph's model: a `midi-routing` stage is the MIDI
   *  routing (its mode, send-to-all by default), a `launchpad` stage the controller (as setController takes it, from
   *  the stage's config, its cart's own SYNC unread); a model with neither routes no MIDI and runs no controller.
   *  The routing runs before the controller whatever order they come in, as a project has always ordered them.
   *  False when the document did not plan. */
  setProjectStages(stages: RoleInstance[]): boolean {
    const routing = stages.find((r) => r.kind === "midi-routing");
    const launchpad = stages.find((r) => r.kind === "launchpad");
    return this.change(() => {
      this.midiRouting = routing ? ((routing.config.mode as MidiRouting | undefined) ?? "sendToAll") : null;
      this.controller = launchpad ? controllerOf(launchpad.config) : null;
    }, false);
  }

  /** The project's MIDI routing, or null to route none. False when the document did not plan. */
  setMidiRouting(mode: MidiRouting | null): boolean {
    return this.change(() => (this.midiRouting = mode), false);
  }

  /** The control-surface session, or null for none: its app config is parsed by the app's schema here, as the
   *  project's is, and buildGraph resolves its target and forces its cart's sync mode as it does a project's. False
   *  when the document did not plan. */
  setController(controller: GraphController | null): boolean {
    const parsed = controller && { ...controller, appConfig: parseAppConfig(controller.app, controller.appConfig) };
    return this.change(() => (this.controller = parsed), false);
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
    const midiRouting = this.midiRouting;
    const controller = this.controller;
    edit();
    if (this.publish(rearmTap)) return true;
    this.systems = systems;
    this.routing = routing;
    this.midiRouting = midiRouting;
    this.controller = controller;
    // EngineGraph keeps a refused document's systems as its model, so hand it the restored one: otherwise a later
    // shape change would place the systems of the edit just rolled back.
    this.publish(false);
    return false;
  }

  private publish(rearmTap: boolean): boolean {
    const { systems, routing, midiRouting, controller } = this;
    return this.graph.publish({ systems, routing, midiRouting, controller }, { rearmTap });
  }
}

/** A `launchpad` stage's config, read as the kernel's role read it, as the controller: its app config parsed by the
 *  app's schema, its cart's own SYNC unread. */
function controllerOf(config: Record<string, unknown>): GraphController {
  const anchorRows = Array.isArray(config.anchorRows) ? (config.anchorRows as (number | null)[]) : [];
  const anchorSeq = typeof config.anchorSeq === "number" ? config.anchorSeq : 0;
  return {
    app: typeof config.app === "string" ? config.app : "",
    target: (config.target as ControllerTarget | undefined) ?? "system",
    systemId: typeof config.systemId === "number" ? config.systemId : 0,
    appConfig: parseAppConfig(typeof config.app === "string" ? config.app : "", config.appConfig ?? {}),
    songRowTicks: Array.isArray(config.songRowTicks) ? (config.songRowTicks as RowTicksTable) : [],
    anchor: anchorRows.length > 0 || anchorSeq !== 0 ? { rows: anchorRows, seq: anchorSeq } : null,
    cartSync: null,
  };
}

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
