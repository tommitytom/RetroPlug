// The .rplg zip entry-key contract — the TS port of the key shape in
// packages/native/src/project/ProjectBinaries.hpp. A `.rplg` is a PKZIP blob
// with one `project.json` entry plus a per-blob entry at a deterministic key:
//
//   systems/{i}/rom
//   systems/{i}/sram
//   systems/{i}/state
//   systems/{i}/roles/{r}/kits/{k}/compiled
//
// The native snapshot/apply primitives produce/consume the blob bytes (they
// touch the live emulator + config structs); THIS module owns the key format —
// projectSerialization uses it to assemble the zip and to separate the config
// entry from the blob entries on load.

export const PROJECT_JSON = "project.json";

export type BlobKind = "rom" | "sram" | "state" | "kit";

export interface BlobKey {
  systemIndex: number;
  kind: BlobKind;
  roleIndex?: number; // kit only
  kitIndex?: number;  // kit only — the k index in the key (position, not LSDj slot)
}

export function blobKey(k: BlobKey): string {
  const base = `systems/${k.systemIndex}/`;
  if (k.kind === "kit") return `${base}roles/${k.roleIndex}/kits/${k.kitIndex}/compiled`;
  return base + k.kind;
}

// Parse a zip entry name into a BlobKey, or null for `project.json` / anything
// that isn't a recognised blob entry.
export function parseBlobKey(name: string): BlobKey | null {
  if (name === PROJECT_JSON) return null;
  const simple = /^systems\/(\d+)\/(rom|sram|state)$/.exec(name);
  if (simple) return { systemIndex: +simple[1], kind: simple[2] as BlobKind };
  const kit = /^systems\/(\d+)\/roles\/(\d+)\/kits\/(\d+)\/compiled$/.exec(name);
  if (kit) return { systemIndex: +kit[1], kind: "kit", roleIndex: +kit[2], kitIndex: +kit[3] };
  return null;
}

export function isBlobEntry(name: string): boolean {
  return name !== PROJECT_JSON;
}
