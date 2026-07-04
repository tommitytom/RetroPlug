// Detect + repair project files that reference source assets (a system's ROM,
// a paired `.sav`, an LSDj kit's sample WAVs) which no longer exist on disk. The
// thin JSON save stores those by path, so a moved file makes a load incomplete;
// the UI shows a "locate missing files" menu before applying the project.
//
// The TS port of packages/native/src/project/ProjectMissingFiles.hpp. "Blob
// embedded" (romBytes / sram / savestate / compiled-kit) becomes "is the keyed
// zip entry present" — the caller passes the set of blob-entry names from the
// unzip, and we test membership via the same blobKey() contract the native codec
// uses. Items are addressed by config index (systemIndex / kitSlot / sampleIndex)
// — the systems don't exist yet at load time, so there are no SystemIds to use.

import { blobKey } from "./projectBinaries";
import type { ProjectConfig, SameBoyConfig } from "./projectConfig";

export interface MissingFile {
  systemIndex: number;          // index into ProjectConfig.systems
  itemKind: "rom" | "sram" | "sample";
  path: string;                 // the missing path (for display + matching)
  kitSlot: number;              // sample only (LSDj kit slot; -1 for rom/sram)
  sampleIndex: number;          // sample only (index into kit.samples; -1 otherwise)
}

type FileExists = (path: string) => boolean;

const basename = (p: string): string => p.split(/[\\/]/).pop() ?? p;
const dirname = (p: string): string => {
  const i = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"));
  return i < 0 ? "" : p.slice(0, i);
};
const isAbsolute = (p: string): boolean => /^([a-zA-Z]:[\\/]|[\\/])/.test(p);

// Every referenced-but-absent file, in config order. Mirrors romPresent/savPresent:
// a ROM is present if bundled (embedded marker, or a `systems/{i}/rom` blob) or its
// path exists; a paired save is present unless it names an explicit savPath with no
// bytes (sram/state blob) and no file (an empty savPath is the suffix sibling, which
// may legitimately be absent for a fresh cart).
export function scanMissingFiles(
  cfg: ProjectConfig,
  blobKeys: ReadonlySet<string>,
  exists: FileExists,
): MissingFile[] {
  const out: MissingFile[] = [];
  cfg.systems.forEach((sys, i) => {
    const romKey  = blobKey({ systemIndex: i, kind: "rom" });
    const sramKey = blobKey({ systemIndex: i, kind: "sram" });
    const stateKey = blobKey({ systemIndex: i, kind: "state" });

    const embeddedRom = sys.kind === "sameboy" && !!(sys as SameBoyConfig).embeddedRom;
    const romOk = embeddedRom || blobKeys.has(romKey) || (!!sys.romPath && exists(sys.romPath));
    if (!romOk) out.push({ systemIndex: i, itemKind: "rom", path: sys.romPath ?? "", kitSlot: -1, sampleIndex: -1 });

    const savOk = !sys.savPath || blobKeys.has(sramKey) || blobKeys.has(stateKey) || exists(sys.savPath);
    if (!savOk) out.push({ systemIndex: i, itemKind: "sram", path: sys.savPath ?? "", kitSlot: -1, sampleIndex: -1 });

    if (sys.kind === "sameboy") {
      (sys as SameBoyConfig).roles?.forEach((role, r) => {
        role.kits?.forEach((kit, k) => {
          // Bundled kit (a compiled blob is present) needs no source WAVs.
          if (blobKeys.has(blobKey({ systemIndex: i, kind: "kit", roleIndex: r, kitIndex: k }))) return;
          kit.samples.forEach((sample, s) => {
            if (!exists(sample.path)) {
              out.push({ systemIndex: i, itemKind: "sample", path: sample.path,
                         kitSlot: kit.slot ?? -1, sampleIndex: s });
            }
          });
        });
      });
    }
  });
  return out;
}

// Point a missing item at `newPath` (mutates the parsed config in place). ROM /
// sram set the path (the blob is absent — that's why it was flagged); sample sets
// the sample path on the kit matched by slot. Returns false if indices don't resolve.
export function relinkInConfig(cfg: ProjectConfig, item: MissingFile, newPath: string): boolean {
  const sys = cfg.systems[item.systemIndex];
  if (!sys) return false;

  if (item.itemKind === "rom")  { sys.romPath = newPath; return true; }
  if (item.itemKind === "sram") { sys.savPath = newPath; return true; }

  // sample — SameBoy only.
  if (sys.kind !== "sameboy") return false;
  for (const role of (sys as SameBoyConfig).roles ?? []) {
    for (const kit of role.kits ?? []) {
      if ((kit.slot ?? -1) !== item.kitSlot) continue;
      if (item.sampleIndex < 0 || item.sampleIndex >= kit.samples.length) return false;
      kit.samples[item.sampleIndex].path = newPath;
      return true;
    }
  }
  return false;
}

// After locating one file, look in its folder for the other still-missing files
// by basename and relink any matches. Lets one pick fix a whole moved folder.
// Returns the number of additional items resolved.
export function autoFindSiblings(
  cfg: ProjectConfig,
  newDir: string,
  blobKeys: ReadonlySet<string>,
  exists: FileExists,
): number {
  let resolved = 0;
  for (const item of scanMissingFiles(cfg, blobKeys, exists)) {
    const candidate = (newDir ? newDir + "/" : "") + basename(item.path);
    if (!exists(candidate)) continue;
    if (relinkInConfig(cfg, item, candidate)) resolved++;
  }
  return resolved;
}

// Rebase the config's relative asset paths (romPath / savPath / kit sample WAVs)
// to absolute against `baseDir` — the TS side of ProjectPaths::toAbsolute (load
// only needs the join; the realpath-hard toRelative stays native for save). A
// lexical join is enough: the OS resolves it for existence + slurp, and a later
// native save re-canonicalizes. Absolute + empty fields are left untouched.
export function toAbsolute(cfg: ProjectConfig, baseDir: string): void {
  const join = (rel: string): string => {
    if (!rel || isAbsolute(rel) || !baseDir) return rel;
    const combined = baseDir.replace(/[\\/]+$/, "") + "/" + rel;
    const out: string[] = [];
    for (const s of combined.split(/[\\/]+/)) {
      if (s === "" || s === ".") continue;
      if (s === "..") {
        if (out.length && out[out.length - 1] !== "..") out.pop();
        else out.push("..");
      } else out.push(s);
    }
    const body = out.join("/");
    return combined.startsWith("/") ? "/" + body : body;
  };
  for (const sys of cfg.systems) {
    if (sys.romPath) sys.romPath = join(sys.romPath);
    if (sys.savPath) sys.savPath = join(sys.savPath);
    if (sys.kind === "sameboy") {
      for (const role of (sys as SameBoyConfig).roles ?? [])
        for (const kit of role.kits ?? [])
          for (const sample of kit.samples) sample.path = join(sample.path);
    }
  }
}

// Convenience: the set of blob-entry names from an unzip result (everything that
// isn't the config JSON), for the scan's "is this blob bundled" test.
export { basename, dirname };
