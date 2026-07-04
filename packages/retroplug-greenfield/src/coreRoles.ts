// The built-in backend "system" roles — a backend's tunable emulator settings,
// carried as roles rather than baked into the core system config (so backend-specific
// knobs like SameBoy's `highpass` never sit in the generic config). These are core
// (built-in backends), but ride the same generic registry a third-party extension
// would use. Each role's config is a zod schema (roleSchema.ts): shape + defaults +
// clamping in one place. Ranges mirror the native enums (SameBoyConfig.hpp: model
// 0..13, highpass 0..2, linkGroupId 0..255).

import type { RoleRegistry } from "./systemRoles";
import { z, clampedInt, boolField, stringField } from "./configSchema";

/** Register the built-in backend system roles into `registry`. */
export function registerCoreRoles(registry: RoleRegistry): void {
  // SameBoy: model / highpass / link group / fast boot.
  registry.registerRole({
    kind: "sameboy",
    category: "system",
    schema: z.object({
      model: clampedInt(0, 13, 9), // SameBoyModel 0..13 (CgbC=9)
      highpass: clampedInt(0, 2, 1), // Off / Accurate / RemoveDcOffset
      linkGroupId: clampedInt(0, 255, 0),
      fastBoot: boolField(true),
    }),
  });

  // GBA: skip-boot-screen (its fastBoot) / bios path.
  registry.registerRole({
    kind: "gba",
    category: "system",
    schema: z.object({
      skipBootScreen: boolField(true),
      fastBoot: boolField(true),
      biosPath: stringField(""),
    }),
  });

  // NES has no backend-specific knobs → no system role.
}
