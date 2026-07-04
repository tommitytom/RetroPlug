// TS view of the persisted ProjectConfig (packages/native/src/project/
// ProjectConfig.hpp + system/SystemConfig.hpp). This is the THIN shape: the
// binary blobs (romBytes / sram / savestate / kit compiledBytes) are stripped
// out to separate zip entries, so they're omitted here — the config JSON that
// crosses the boundary carries only structure + paths.
//
// The per-system tagged union discriminates on "kind" ("sameboy" | "nes" |
// "gba"), matching rfl::TaggedUnion<"kind", ...>. Fields absent from an older
// file take their defaults on the native (reflect-cpp DefaultIfMissing) read,
// so this type is deliberately permissive (most fields optional).

export type SystemKind = "sameboy" | "nes" | "gba";

// One source sample of an LSDj kit (path + optional processing metadata). The
// compiled 16 KB bank is a blob and lives in a separate zip entry, not here.
export interface KitSampleConfig {
  path: string;
  name?: string;
  offset?: number;
  length?: number;
  // effects: opaque here (not needed for the transforms in this layer).
  pitch?: number;
  volume?: number;
}

export interface LsdjKitConfig {
  slot?: number;
  name?: string;
  compiledHash?: number;
  samples: KitSampleConfig[];
}

// A role is a tagged union too ("type"). Only the LSDj kit-patch role carries
// paths this layer cares about; other roles pass through untouched.
export interface RoleConfig {
  type: string;
  // Present on the "lsdj-kit-patch" role.
  kits?: LsdjKitConfig[];
  [k: string]: unknown;
}

interface SystemConfigCommon {
  kind: SystemKind;
  romPath?: string;
  savPath?: string;
  [k: string]: unknown;
}

export interface SameBoyConfig extends SystemConfigCommon {
  kind: "sameboy";
  // A non-empty embeddedRom marker means the ROM is bundled, not read from disk.
  embeddedRom?: string;
  savSuffix?: string;
  roles?: RoleConfig[];
}
export interface MesenNesConfig extends SystemConfigCommon { kind: "nes"; }
export interface MesenGbaConfig extends SystemConfigCommon { kind: "gba"; biosPath?: string; }

export type SystemConfig = SameBoyConfig | MesenNesConfig | MesenGbaConfig;

export interface ProjectSettings {
  layout?: number;
  midiRouting?: number;
  audioRouting?: number;
  zoom?: number;
}

export interface ProjectConfig {
  schemaVersion: string;
  settings?: ProjectSettings;
  systems: SystemConfig[];
}
