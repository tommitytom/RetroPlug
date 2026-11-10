// The project service a package contributes (docs/modular-graph-phase-f5a-design.md section 6.2): the control-plane
// half of a feature whose state outlives a publish - the controller's poll of the cart it drives is the one today. The
// shell builds one INSTANCE per factory per project store, because a plugin host runs one control plane per instance
// (the multi-instance hazard class of the project's memory), so a service's state is its store's and two stores share
// nothing.
//
// The contract lives here rather than in core because its context names TrackerPlayback (this package's), and core
// cannot import a package above it (decision 13). A package contributes the factory from its app module's
// `projectServices` list, which the generated registry concatenates in product order.
import type { RoleInstance } from "@retroplug/core/systemRoles";
import type { TrackerPlayback } from "./trackerPlayback";

/** One service instance's view of the store: the section it owns, the systems, and the reads a poll may make. Exactly
 *  every read a poll makes: the systems list and each one's id, ROM path and roles, the battery, the work RAM, a ROM's
 *  header prefix, and the tracker integration's playback hook. */
export interface ProjectServiceContext {
  /** This service's section, as its contributor parses it. */
  section<T = unknown>(): T;
  /** The live systems, in slot order. */
  systems(): readonly {
    readonly id: number;
    readonly romPath: string;
    readonly roles: readonly RoleInstance[];
  }[];
  /** A system's battery, or null when it has none (a real Game Boy, a system that is gone). */
  readSram(id: number): Uint8Array | null;
  /** A system's live work RAM, or null when it cannot be read (an emulated cart has it, a real one does not). */
  readRam(id: number): Uint8Array | null;
  /** The first `bytes` of a file, or null: a ROM's header, which decides how its playback reads the cart's memory. */
  readFilePrefix(path: string, bytes: number): Uint8Array | null;
  /** The playback of the tracker integration these roles name, or null for a system with no tracker. */
  playbackOf(roles: readonly RoleInstance[]): TrackerPlayback | null;
}

/** What a service may do, all of it optional: a service that only publishes a model has no `poll` and no `state`. */
export interface ProjectService {
  /** One poll, on the cadence the UI's song watch calls it at (~30 frames, editor-driven). True = something the graph
   *  reads moved, so the store publishes and notifies. */
  poll?(ctx: ProjectServiceContext): boolean;
  /** The model the graph places for this service's key (`GraphModel.models[key]`), or null for none. Called at every
   *  publish, so it must be cheap enough to run on any edit. */
  graphModel?(ctx: ProjectServiceContext): unknown | null;
  /** What a UI may read of the service, through `host.project.serviceState(key)` (the controller cart's polled
   *  SYNC). */
  state?(ctx: ProjectServiceContext): unknown;
}

/** A package's service contribution: the key its model and its section share, and the instance factory. */
export interface ProjectServiceFactory {
  /** The model / section key ("controller"). */
  readonly key: string;
  create(): ProjectService;
}
