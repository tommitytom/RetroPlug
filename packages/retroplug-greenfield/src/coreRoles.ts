// The built-in backend "system" roles — a backend's tunable emulator settings,
// carried as roles rather than baked into the core system config (so backend-specific
// knobs like SameBoy's `highpass` never sit in the generic config). These are core
// (built-in backends), but ride the same generic registry a third-party extension
// would use. Ranges mirror the native enums (SameBoyConfig.hpp: model 0..13,
// highpass 0..2, linkGroupId 0..255).

import type { RoleRegistry } from "./systemRoles";

function clampInt(v: unknown, min: number, max: number, def: number): number {
  if (typeof v !== "number" || !Number.isFinite(v)) return def;
  return Math.max(min, Math.min(max, Math.trunc(v)));
}
function toBool(v: unknown, def: boolean): boolean {
  return typeof v === "boolean" ? v : def;
}
function toStr(v: unknown, def: string): string {
  return typeof v === "string" ? v : def;
}

/** Register the built-in backend system roles into `registry`. */
export function registerCoreRoles(registry: RoleRegistry): void {
  // SameBoy: model / highpass / link group / fast boot.
  registry.registerRole({
    kind: "sameboy",
    category: "system",
    defaultConfig: () => ({ model: 9, highpass: 1, linkGroupId: 0, fastBoot: true }),
    clampConfig: (c) => ({
      model: clampInt(c.model, 0, 13, 9), // SameBoyModel 0..13 (CgbC=9)
      highpass: clampInt(c.highpass, 0, 2, 1), // Off/Accurate/RemoveDcOffset
      linkGroupId: clampInt(c.linkGroupId, 0, 255, 0),
      fastBoot: toBool(c.fastBoot, true),
    }),
  });

  // GBA: skip-boot-screen (its fastBoot) / bios path.
  registry.registerRole({
    kind: "gba",
    category: "system",
    defaultConfig: () => ({ skipBootScreen: true, fastBoot: true, biosPath: "" }),
    clampConfig: (c) => ({
      skipBootScreen: toBool(c.skipBootScreen, true),
      fastBoot: toBool(c.fastBoot, true),
      biosPath: toStr(c.biosPath, ""),
    }),
  });

  // NES has no backend-specific knobs → no system role.
}
