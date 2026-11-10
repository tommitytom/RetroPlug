// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down), each system's roles with a
// protocol node (docs/modular-graph-phase-b3b-design.md section 4), none until set, its core config and gain, its
// node's knobs (docs/modular-graph-phase-c2-design.md section 5), none until set (so its core keeps the settings it was
// built with, while the params it does not name start at their defaults at its first block),
// and the project's MIDI routing and each package's placer model, none until set (so no MIDI is routed to a system and
// no package places a node). It publishes after each call but setParam, which sets a param live. Use one per process,
// directGraph(): two publishers with separate EngineGraphs would each drop the other's systems from the document.
// Sharing a session's EngineGraph (so the session's render calls render what this places) does not merge the two models
// either: each publish replaces the graph's model with the publisher's own systems. It is safe only while the
// session's stores place nothing, the two publishing in turn, as test-native/parity/render-blocks.test.ts and
// packages/lsdj/examples/analyze-lsdj-sync.ts do.
import { createRealBackend } from "../realBackend";
import { MidiRouting } from "@retroplug/core/midiRouting";
import type { AudioRouting } from "../settingsEnums";
import type { RoleInstance } from "@retroplug/core/systemRoles";
import type { ConstructSpec, EmulatorBackend, HostBackend } from "../backend";
import { EngineGraph } from "../engineGraph";
import { capabilitiesOf, exportModeOf, linkGroupOf, systemKnobs, systemNodeId, type GraphSystem } from "./buildGraph";
import { bootSystem, ResourcePool, systemHandles, type BootedSystem } from "./systemResources";
import { PLACERS } from "../generated/placers";
// The product's registrations, for their load-time side effect: a package's placer normalizes its model through the
// registry its contribution kind fills, and this publisher parses no project, so nothing else here would fill it. A
// module that reads a kind-filled registry imports the generated registry (projectConfig.ts states the rule).
import "../generated/packages";
import { protocolPipeline, type LeftOutRole } from "./protocolNodes";

export type DirectBackend = Pick<EmulatorBackend, "putResource" | "releaseResource" | "setGraph" | "setParam"> &
  Pick<HostBackend, "fileExists">;

// Whether `o` owns `key`: `in` also finds every key Object.prototype carries, so `{toString: 1}` would count as a
// setting field.
const has = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

export class DirectGraph {
  readonly graph: EngineGraph;
  private systems: GraphSystem[] = [];
  private routing: AudioRouting = "stereo";
  private midiRouting: MidiRouting | null = null;
  /** The model each placer places, by key: none until set, so nothing of any package's is placed. */
  private models: Record<string, unknown> = {};
  /** The handles on the resources each placed system's node boots from (graph/systemResources.ts). */
  private readonly pool: ResourcePool;

  constructor(
    private readonly backend: DirectBackend,
    graph?: EngineGraph,
  ) {
    this.graph = graph ?? new EngineGraph(backend);
    this.pool = new ResourcePool(backend);
  }

  /** The systems this graph places, in slot order. */
  ids(): number[] {
    return this.systems.map((s) => s.id);
  }

  /** Boot a system from `spec` under `id` and append it, with no roles. False when its resources would not put, or the
   *  graph did not plan (the resources are then given back). */
  add(spec: ConstructSpec, id: number): boolean {
    const placed = this.place(id, spec);
    if (!placed) return false;
    return this.change(() => this.systems.push(placed.system), true, placed.handles);
  }

  /** Boot a system from `spec` under `id` in `oldId`'s slot, with no roles (a new system: the protocol nodes of
   *  `oldId` go with it, and so does its ROM resource once the new document plans). False when `oldId` is absent (the
   *  tap is re-armed all the same, as a replace of a gone system always did), or when its resources would not put. */
  replace(oldId: number, spec: ConstructSpec, id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === oldId);
    if (at < 0) {
      this.publish(true);
      return false;
    }
    const placed = this.place(id, spec);
    if (!placed) return false;
    return this.change(
      () => (this.systems[at] = placed.system),
      true,
      placed.handles,
      systemHandles(this.systems[at].resources),
    );
  }

  /** Drop system `id`; later slots shift down, and its resources go once the document that stops naming them has
   *  planned. An id already gone re-arms the tap and changes nothing else. */
  remove(id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return this.publish(true);
    return this.change(() => this.systems.splice(at, 1), true, [], systemHandles(this.systems[at].resources));
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

  /** The model the product's placer for `key` places (docs/modular-graph-phase-f5a-design.md section 6.3), or null for
   *  none: its app config is parsed by the package's own `normalize` here, as the project's publisher does, and the
   *  placer resolves its target and forces its cart's sync mode as it does a project's. A key no listed package places
   *  is still stored and published, and places nothing. False when the document did not plan. */
  setModel(key: string, model: unknown | null): boolean {
    const normalize = PLACERS.find((entry) => entry.placer.key === key)?.placer.normalize;
    const parsed = model !== null && model !== undefined && normalize ? normalize(model) : model;
    return this.change(() => {
      const models = { ...this.models };
      if (parsed === null || parsed === undefined) delete models[key];
      else models[key] = parsed;
      this.models = models;
    }, false);
  }

  /** Edit system `id`'s core config as the stores' core-config role spells it (`sameboy` or `mesen`,
   *  the sameboy and mesen packages: an
   *  enum as its label, never an ordinal), merged over what this graph gave it before: none at first, so its node
   *  carries no setting field and its core keeps the settings it was built with (the params it does not name start at
   *  their defaults at its first block). Its setting fields ride its node's settings and a NES's expansionVolume its
   *  params (buildGraph.ts systemKnobs), each reaching the core at the start of the next block, and a Game Boy's
   *  linkGroupId is its link cable. It publishes, the param included (a document's param value reaches the node when
   *  the node does not hold it, a live `setParam`'s value included). False, changing nothing, when `id` is not placed
   *  here, for a field its node does not take (a construct-time one, another core's, or a value the node would
   *  refuse, an ordinal among them, which buildGraph would leave out, the core silently keeping what it has), or when
   *  the document did not plan. */
  setCoreConfig(id: number, partial: Record<string, unknown>): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return false;
    const system = this.systems[at];
    const knobs = systemKnobs({ type: system.type, coreConfig: partial });
    // The link cable is wiring, not a knob: the field its console declares (capabilities.linkCable) is taken when its
    // value is a group, and the key it is read from here is that field's, never a literal of the shell's.
    const cable = capabilitiesOf(system.type).linkCable?.field;
    const group = cable === undefined ? undefined : partial[cable];
    const isGroup = typeof group === "number" && Number.isInteger(group) && group >= 0 && group <= 255;
    const takes = (field: string): boolean =>
      has(knobs.settings, field) || has(knobs.params, field) || (cable === field && isGroup);
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

  // Apply an edit to the model and publish; a refused document puts the model back, gives back the resources the edit
  // put and releases the ones it replaced only once a document naming the new model has planned. A publish that throws
  // is the engine's or the transport's failure rather than a refusal it answered, and the graph counts it as refused
  // (the engine keeps the plan it ran), so it takes the same rollback before the error reaches the caller.
  private change(edit: () => unknown, rearmTap: boolean, put: number[] = [], drop: number[] = []): boolean {
    const systems = this.systems.slice();
    const routing = this.routing;
    const midiRouting = this.midiRouting;
    const models = this.models;
    const rollback = (): void => {
      this.systems = systems;
      this.routing = routing;
      this.midiRouting = midiRouting;
      this.models = models;
      for (const id of put) {
        try {
          this.pool.drop(id);
        } catch (e) {
          console.warn(`[graph] giving back a change's resource threw too: ${String(e)}`);
        }
      }
    };
    edit();
    try {
      if (this.publish(rearmTap)) {
        for (const id of drop) this.pool.drop(id);
        return true;
      }
    } catch (e) {
      rollback();
      this.resendRestored();
      throw e;
    }
    rollback();
    this.resendRestored();
    return false;
  }

  // EngineGraph keeps a refused document's systems as its model, so hand it the restored one: otherwise a later shape
  // change would place the systems of the edit just rolled back. Its failure is said, never thrown, since the caller's
  // own is the one it must see.
  private resendRestored(): void {
    try {
      this.publish(false);
    } catch (e) {
      console.warn(`[graph] the restored model's send threw too: ${String(e)}`);
    }
  }

  /** Put `spec`'s resources and the system its node places, or nothing with the reason logged when one would not
   *  put. */
  private place(id: number, spec: ConstructSpec): { system: GraphSystem; handles: number[] } | null {
    const booted: BootedSystem | { error: string } = bootSystem(this.pool, spec);
    if ("error" in booted) {
      console.warn(`[graph] system ${id}: ${booted.error}`);
      return null;
    }
    const system: GraphSystem = {
      id,
      platform: spec.platform,
      type: booted.type,
      linkGroup: linkGroupOf(booted.type, spec.coreConfig),
      exportMode: exportModeOf(booted.type, spec.coreConfig),
      resources: booted.resources,
      romName: booted.romName,
      pipeline: [],
    };
    return { system: spec.coreConfig ? { ...system, coreConfig: spec.coreConfig } : system, handles: booted.handles };
  }

  private publish(rearmTap: boolean): boolean {
    const { systems, routing, midiRouting, models } = this;
    return this.graph.publish({ systems, routing, midiRouting, models }, { rearmTap });
  }
}

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
