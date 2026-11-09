// What a capability job asks and answers (docs/modular-graph-phase-c5-design.md section 6): a node of the running
// graph, asked to read, write or dump something of the system behind it, answers one of these operations. The shell's
// job pump (packages/retroplug/src/capability.ts) submits them and settles their promises, and re-exports these types;
// they live here because a host names them - `Host.systems.capability` - so a hook, a cart's menu or a console's TS can
// type a job without importing the shell.

/** The operations, as `<capability>.<op>`; each names its versioned native interface below. */
export type CapabilityOp =
  | "memory.regions"
  | "memory.read"
  | "memory.write"
  | "sram.dump"
  | "sram.restore"
  | "rom.header"
  | "rom.title"
  | "rom.upload";

/** The arguments an operation reads; the rest are ignored. memory: `region` by name, `offset`, and `length` for a read
 * (absent or 0: to the region's end) or `bytes` for a write; sram: `bytes` for a restore; rom: `length` for a header
 * (absent or 0: the whole image), and `bytes` and `name` (the file it is stored under) for an upload, which only a
 * device's node (rp.n8.nes) performs. On that node `name` also names the ROM whose battery file (EDN8/gamedata/<rom>/
 * bram.srm on its SD card) sram.dump and sram.restore reach, defaulting to the ROM this host last booted. */
export interface CapabilityArgs {
  region?: string;
  offset?: number;
  length?: number;
  bytes?: Uint8Array;
  name?: string;
}

/** What a cart's `sram.restore` wrote: the battery file, and whether the ROM's folder had to be made for it (a ROM that
 * never saved on the card, or a mistyped name). Only a device's node restores in place; an emulated system refuses. */
export interface SramRestore {
  path: string;
  created: boolean;
}

/** What a cart's `rom.upload` booted: where the image went, the mapper core the cart's menu chose, and that streaming
 * stopped (a running game has no menu, so the link is reconnected to the new ROM). */
export interface RomUpload {
  path: string;
  mapIndex: number;
  streaming: "stopped";
}

/** A region `memory.regions` lists: its name and its published size in bytes. */
export interface MemoryRegion {
  name: string;
  size: number;
}

/** What each operation's promise resolves with. */
export interface CapabilityResults {
  "memory.regions": MemoryRegion[];
  "memory.read": Uint8Array;
  "memory.write": void;
  "sram.dump": Uint8Array;
  "sram.restore": SramRestore | undefined;
  "rom.header": Uint8Array;
  "rom.title": string;
  "rom.upload": RomUpload;
}

/** A long job's progress: `progress` of `total` (units the job chooses: bytes for a transfer). */
export interface CapabilityProgress {
  progress: number;
  total: number;
}
