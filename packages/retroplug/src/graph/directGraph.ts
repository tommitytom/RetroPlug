// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down), each system's roles with a
// protocol node (docs/modular-graph-phase-b3b-design.md section 4), none until set, its core config and gain, its
// node's knobs (docs/modular-graph-phase-c2-design.md section 5), none until set (so its core keeps the settings it was
// built with, while the params it does not name start at their defaults at its first block),
// and the project's MIDI routing and controller, none until set (so no MIDI is routed to any system). It publishes
// after each call but setParam, which sets a param live. Use one per process, directGraph(): two publishers with
// separate EngineGraphs would each drop the other's systems from the document. Sharing a session's EngineGraph (so
// the session's render calls render what this places) does not merge the two models either: each publish replaces
// the graph's model with the publisher's own systems. It is safe only while the session's stores place nothing, the
// two publishing in turn, as test-native/parity/render-blocks.test.ts and cli/sessions/analyze-lsdj-sync.ts do.
import { createRealBackend } from "../realBackend";
import type { AudioRouting, MidiRouting } from "../settingsEnums";
import type { RoleInstance } from "../systemRoles";
import type { ConstructSpec, EmulatorBackend } from "../backend";
import { EngineGraph } from "../engineGraph";
import { parseAppConfig } from "../controller/registry";
import { systemKnobs, systemNodeId, type GraphSystem } from "./buildGraph";
import type { GraphController } from "./controller";
import { protocolPipeline, type LeftOutRole } from "./protocolNodes";

export type DirectBackend = Pick<EmulatorBackend, "buildCore" | "setGraph" | "setParam">;

// Whether `o` owns `key`: `in` also finds every key Object.prototype carries, so `{toString: 1}` would count as a
// setting field.
const has = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

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

  /** System `id`'s roles, in order, placed as protocolPipeline places them: each with a protocol node as one, with its
   *  config as given as the node's settings (a role kept from the last call keeps its node and state). A role with no
   *  node is left out, and so is one whose kind is already placed or whose node would write an input an earlier
   *  role's does, which `leftOut` hears of: the document is sent without it. False when `id` is not placed here, or the
   *  document did not plan. */
  setPipeline(id: number, roles: RoleInstance[], leftOut?: (role: LeftOutRole) => void): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return false;
    const pipeline = protocolPipeline(roles, leftOut);
    return this.change(() => (this.systems[at] = { ...this.systems[at], pipeline }), false);
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

  /** Edit system `id`'s core config as the stores' core-config role spells it (`sameboy` or `mesen`, coreRoles.ts: an
   *  enum as its label, never an ordinal), merged over what this graph gave it before: none at first, so its node
   *  carries no setting field and its core keeps the settings it was built with (the params it does not name start at
   *  their defaults at its first block). Its setting fields ride its node's settings and a NES's expansionVolume its
   *  params (buildGraph.ts systemKnobs), each reaching the core at the start of the next block, and a Game Boy's linkGroupId is its link cable. It publishes, the param included (a document's param value
   *  reaches the node when it differs from the one last sent). False, changing nothing, when `id` is not placed here,
   *  for a field its node does not take (a construct-time one, another core's, or a value the node would refuse, an
   *  ordinal among them, which buildGraph would leave out, the core silently keeping what it has), or when the
   *  document did not plan. */
  setCoreConfig(id: number, partial: Record<string, unknown>): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return false;
    const system = this.systems[at];
    const knobs = systemKnobs({ type: system.type, coreConfig: partial });
    const group = partial.linkGroupId;
    const isGroup = typeof group === "number" && Number.isInteger(group) && group >= 0 && group <= 255;
    const takes = (field: string): boolean =>
      has(knobs.settings, field) ||
      has(knobs.params, field) ||
      (field === "linkGroupId" && system.type === "rp.sameboy.gb" && isGroup);
    if (!Object.keys(partial).every(takes)) return false;
    const coreConfig = { ...system.coreConfig, ...partial };
    const linkGroup = typeof group === "number" ? group : system.linkGroup;
    return this.change(() => (this.systems[at] = { ...system, coreConfig, linkGroup }), false);
  }

  /** A live change to a param of system `id`'s node: its `gain` (dB), or a NES's `expansionVolume` (percent), set
   *  through backend.setParam at the start of the next block with no publish, and kept in the model, so a document
   *  published later carries the value the node runs, as the store keeps one. False, keeping nothing, when `id` is not
   *  placed here, for a param no system node has, or when the engine refuses it (a param this node does not have, a
   *  NaN). */
  setParam(id: number, param: string, value: number): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0 || (param !== "gain" && param !== "expansionVolume")) return false;
    if (!this.backend.setParam(systemNodeId(id), param, value)) return false;
    const system = this.systems[at];
    this.systems[at] =
      param === "gain"
        ? { ...system, gainDb: value }
        : { ...system, coreConfig: { ...system.coreConfig, [param]: value } };
    // The engine's node now holds a value its last document does not, so the graph's model takes it too (a shape
    // change must not rebuild the stale one) and the next document is sent even if it repeats the last one sent.
    this.graph.systemsMovedLive(this.systems);
    return true;
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

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
