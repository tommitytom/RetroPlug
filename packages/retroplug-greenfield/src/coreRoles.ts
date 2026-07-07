// The built-in core-config "system" roles — a core's tunable emulator settings, carried as roles
// rather than baked into the generic system config (so core-specific knobs like SameBoy's `highpass`
// never sit in the generic config). Keyed by the `core` value (platform.ts), so the role's kind IS its
// core. These are built-in, but ride the same generic registry a third-party extension would use. Each
// role's config is a zod schema (roleSchema.ts): shape + defaults + clamping in one place. Ranges
// mirror the native enums (SameBoyConfig.hpp: model 0..13, highpass 0..2, linkGroupId 0..255).

import type { RoleRegistry } from "./systemRoles";
import { z, clampedInt, boolField } from "./configSchema";

/** Register the built-in core-config system roles into `registry`. */
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

  // Mesen (NES + GBA) exposes no natively-consumed knobs yet → no system role. (The former GBA
  // skipBootScreen/biosPath were inert — the backend ignored the settings blob — so they're dropped
  // until Mesen actually wires them, at which point a "mesen" core role is added here.)
}
