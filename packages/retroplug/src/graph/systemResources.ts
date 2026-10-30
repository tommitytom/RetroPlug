// One place that turns "what to boot" into what a system node names (docs/modular-graph-phase-c3-design.md
// section 6): the ROM and its two seeds as resources in the engine's table, and the ROM's name. The store and
// DirectGraph both boot a system this way, so a boot built for a store entry and one built for a literal
// DirectGraph spec are the same resources with the same settings. buildGraph writes the handles and the name
// into the node it places (graph/buildGraph.ts); a node never touches the filesystem.
//
// The seeds follow the core's own rule: seed bytes when TS holds them (an asset role's image, a battery read back from
// a live core, a project's blob), else the file by path when it is there, else NO seed - a missing `.sav` is a fresh
// cartridge, never a failure. A savestate wins over a battery, as it does at the core.
import type { ConstructSpec, EmulatorBackend, HostBackend, ResourceSpec, ResourceType } from "../backend";
import { systemNodeTypeFor, type SystemNodeType } from "../platform";
import { stem } from "../pathUtil";

/** The two resource calls a boot needs, plus the file check a seed by path is decided with. */
export type ResourceBackend = Pick<EmulatorBackend, "putResource" | "releaseResource"> &
  Pick<HostBackend, "fileExists">;

/** A node's settings name a resource as `"res:<id>"` (packages/native/src/graph/node.hpp): the resource's id in the
 *  engine's table, never a path. */
export const resourceRef = (id: number): string => `res:${id}`;

/** A system's resources, as the store counts and DirectGraph places them: the ROM its core boots from, and the seed
 *  its core starts with (the savestate, or the battery when it has no savestate). */
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

/** A boot that could not put its bytes: the reason the engine gave (an unreadable ROM, the failure the user saw when
 *  `buildCore` answered null), with every handle it had already taken given back. */
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

/** The control plane's counted handles on the engine's resources: a resource is put once and named by every system
 *  that boots from it (an entry's ROM survives its rebuilds, and a duplicate shares its source's), and the engine is
 *  told to drop it when the last handle goes. The engine keeps a dropped resource readable while a plan still pins
 *  it, so a caller drops one only after the document that stops naming it has planned. */
export class ResourcePool {
  private counts = new Map<number, number>();
  private sources = new Map<number, ResourceSpec>();

  constructor(readonly backend: ResourceBackend) {}

  /** Put `spec` in the engine's table and take its first handle: the id, or the reason the engine refused it. */
  put(spec: ResourceSpec): { id: number } | { error: string } {
    const result = this.backend.putResource(spec);
    if ("error" in result) return { error: result.error };
    this.counts.set(result.id, (this.counts.get(result.id) ?? 0) + 1);
    this.sources.set(result.id, spec);
    return { id: result.id };
  }

  /** Whether the resource `id` was put from `spec`: true for the same file, the same embedded marker, or byte-for-byte
   *  the same image. A rebuild whose ROM bytes moved (an asset role folded a different image in) must not reuse the
   *  resource the old core read. */
  sameSource(id: number, spec: ResourceSpec): boolean {
    const was = this.sources.get(id);
    if (!was || was.type !== spec.type) return false;
    if ("path" in was && "path" in spec) return was.path === spec.path;
    if ("embedded" in was && "embedded" in spec) return was.embedded === spec.embedded;
    if (!("bytes" in was) || !("bytes" in spec) || was.bytes.length !== spec.bytes.length) return false;
    for (let i = 0; i < was.bytes.length; ++i) if (was.bytes[i] !== spec.bytes[i]) return false;
    return true;
  }

  /** Take another handle on a resource this pool holds, so a boot can share it: the same id. */
  retain(id: number): number {
    const held = this.counts.get(id) ?? 0;
    if (held === 0) throw new Error(`resource ${id} is not one this pool holds`);
    this.counts.set(id, held + 1);
    return id;
  }

  /** Give one handle back, telling the engine to drop the resource once it was the last. Absent ids are no-ops, so a
   *  caller can hand back the seeds a boot never took. */
  drop(id: number | undefined): void {
    if (id === undefined) return;
    const held = this.counts.get(id) ?? 0;
    if (held > 1) {
      this.counts.set(id, held - 1);
      return;
    }
    this.counts.delete(id);
    this.sources.delete(id);
    if (held === 1) this.backend.releaseResource(id);
  }

}

/** Boot `spec` from resources: put its ROM and its seeds, or answer why the engine refused one of them, having given
 *  back what it had already taken. `opts.rom` is a ROM resource to share when it was put from the same bytes (a rebuild
 *  of the same image, a duplicate of the same cart); a spec whose ROM moved puts its own. */
export function bootSystem(pool: ResourcePool, spec: ConstructSpec, opts: { rom?: number } = {}): BootResult {
  const handles: number[] = [];
  const giveBack = (error: string): BootResult => {
    for (const id of handles) pool.drop(id);
    return { error };
  };
  const source = romSource(spec);
  let rom: number;
  if (opts.rom !== undefined && pool.sameSource(opts.rom, source)) {
    rom = pool.retain(opts.rom);
  } else {
    const put = pool.put(source);
    if ("error" in put) return { error: put.error };
    rom = put.id;
  }
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
