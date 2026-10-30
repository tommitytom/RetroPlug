// One place that turns "what to boot" into what a system node names (docs/modular-graph-phase-c3-design.md
// section 6): the ROM and its two seeds as resources in the engine's table, and the ROM's name. The store and
// DirectGraph both boot a system this way, so a boot built for a store entry and one built for a literal
// DirectGraph spec are the same resources with the same settings. buildGraph writes the handles and the name
// into the node it places (graph/buildGraph.ts); a node never touches the filesystem.
//
// The seeds follow the core's own rule: seed bytes when TS holds them (an asset role's image, a battery read back from
// a live core, a project's blob), else the file by path when it is there, else NO seed - a missing `.sav` is a fresh
// cartridge, never a failure, and so is an empty one, since a file that opens and holds nothing is an empty payload (a
// blank battery) while only a file that cannot be opened is refused. A savestate wins over a battery, as it does at the
// core.
import type { ConstructSpec, EmulatorBackend, HostBackend, ResourceSpec, ResourceType } from "../backend";
import { systemNodeTypeFor, type SystemNodeType } from "../platform";
import { stem } from "../pathUtil";

/** The two resource calls a boot needs, plus the file check a seed by path is decided with. */
export type ResourceBackend = Pick<EmulatorBackend, "putResource" | "releaseResource"> &
  Pick<HostBackend, "fileExists">;

/** A node's settings name a resource as `"res:<id>"` (packages/native/src/graph/node.hpp): the resource's id in the
 *  engine's table, never a path. */
export const resourceRef = (id: number): string => `res:${id}`;

/** A system's resources, as the store counts and DirectGraph places them: the ROM its core boots from, and the seeds
 *  it may start from - the battery, the savestate, or both when both were there, which the node holds and the core
 *  applies in that order (the savestate wins). */
export interface SystemResources {
  rom: number;
  sram?: number;
  state?: number;
}

/** What a boot took and what a node is placed with: the node type its platform names, the resources, and the ROM's
 *  name (a Master System's staged file and battery identity, and an embedded ROM's marker). */
export interface BootedSystem {
  type: SystemNodeType;
  resources: SystemResources;
  romName: string;
  /** Every resource handle this boot holds, in the order it took them: a caller that gives the boot up hands these
   *  back, and a caller that keeps the system drops them when it goes. */
  handles: number[];
}

/** The handles a system's resources hold, in the order a boot took them (the ROM, the battery, the savestate): what a
 *  caller hands back when the system goes. */
export function systemHandles(resources: SystemResources): number[] {
  return [resources.rom, resources.sram, resources.state].filter((id): id is number => id !== undefined);
}

/** A boot that could not put its bytes: the reason the engine gave (an unreadable ROM, a seed it refused), from a put
 *  that happens before any node exists, with every handle it had already taken given back. */
export type BootResult = BootedSystem | { error: string };

/** The ROM's name: the file's stem, or an embedded ROM's marker (`"mgb"`). Mesen stages a Master System or Game Gear
 *  ROM as a file and takes its battery stem from that name; every other console only needs a name that is its own. */
export function romNameOf(spec: Pick<ConstructSpec, "romPath" | "embeddedRom">): string {
  return spec.embeddedRom !== "" ? spec.embeddedRom : stem(spec.romPath);
}

/** The ROM a spec boots from: the bytes an asset role folded in (they already are the effective image), the marker of
 *  a ROM baked into the binary, or the file the engine reads itself (a GBA ROM can be 32 MB, and never crosses). */
function romSource(spec: ConstructSpec): ResourceSpec {
  if (spec.embeddedRom !== "") return { type: "rom", embedded: spec.embeddedRom };
  if (spec.romBytes && spec.romBytes.length > 0) return { type: "rom", bytes: spec.romBytes };
  return { type: "rom", path: spec.romPath };
}

/** A seed's source: the bytes when TS holds them, else the file by path when it is there, else none. */
function seedSource(
  type: ResourceType,
  bytes: Uint8Array | undefined,
  path: string | null,
  there: (p: string) => boolean,
): ResourceSpec | null {
  if (bytes) return { type, bytes };
  if (path && there(path)) return { type, path };
  return null;
}

/** The control plane's handles on the engine's resources: a boot puts the ROM and the seeds its node names, and a
 *  system holds one handle on each for as long as it lives, so the engine is told to drop a resource when the system
 *  that holds it goes. Every boot reads its own bytes - a rebuild re-reads the ROM from its source - so no two systems
 *  share one. The engine keeps a dropped resource readable while a plan still pins it, so a caller drops one only
 *  after the document that stops naming it has planned. */
export class ResourcePool {
  private readonly held = new Set<number>();

  constructor(readonly backend: ResourceBackend) {}

  /** Put `spec` in the engine's table and take its handle: the id, or the reason the engine refused it. */
  put(spec: ResourceSpec): { id: number } | { error: string } {
    const result = this.backend.putResource(spec);
    if ("error" in result) return { error: result.error };
    this.held.add(result.id);
    return { id: result.id };
  }

  /** Give the handle back, telling the engine to drop the resource. Absent ids and ids this pool does not hold are
   *  no-ops, so a caller can hand back the seeds a boot never took. The id is forgotten only once the release got
   *  through: a release the transport died on leaves the engine holding the resource, and this handle is the only way
   *  left to ask for it back, so a caller that survives the throw can offer the same id again. */
  drop(id: number | undefined): void {
    if (id === undefined) return;
    if (!this.held.has(id)) return;
    this.backend.releaseResource(id);
    this.held.delete(id);
  }
}

/** Boot `spec` from resources: put its ROM and its seeds, or answer why the engine refused one of them, having given
 *  back what it had already taken. Every boot puts its own ROM, so a rebuild's file is read again from its source. */
export function bootSystem(pool: ResourcePool, spec: ConstructSpec): BootResult {
  const handles: number[] = [];
  const giveBack = (error: string): BootResult => {
    for (const id of handles) pool.drop(id);
    return { error };
  };
  const put = pool.put(romSource(spec));
  if ("error" in put) return { error: put.error };
  const rom = put.id;
  handles.push(rom);
  let sram: number | undefined;
  let state: number | undefined;
  const there = (p: string): boolean => pool.backend.fileExists(p);
  // The battery first, then the savestate, as a core applies them (a savestate restored over its own battery is the
  // machine it was taken from).
  const sramPut = seedSource("sram", spec.sramBytes, spec.savPath, there);
  if (sramPut) {
    const put = pool.put(sramPut);
    if ("error" in put) return giveBack(put.error);
    sram = put.id;
    handles.push(sram);
  }
  const statePut = seedSource("state", spec.stateBytes, spec.statePath, there);
  if (statePut) {
    const put = pool.put(statePut);
    if ("error" in put) return giveBack(put.error);
    state = put.id;
    handles.push(state);
  }
  return { type: systemNodeTypeFor(spec.platform), resources: { rom, sram, state }, romName: romNameOf(spec), handles };
}
