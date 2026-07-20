// The "risa" feature role. A control-plane-only marker role (no DSP behaviour, no config yet) attached
// to a risa ROM by the risa ROM provider (romProviders.ts). Its presence on a system gates the risa
// Songs menu — `sys.roles.find(r => r.kind === "risa")` — exactly as `lsdj-sync` gates the LSDj menu.
// Registered into the control-plane registry only (buildAppRegistry), like registerLsdjAssetsRole.
//
// M2: just the marker. M3 will grow this (or a sibling `risa-assets` role) an `onConstruct` hook to
// apply non-destructive kit/theme/font overrides via the spec.romBytes channel, mirroring lsdj-assets.
import { z } from "zod";
import type { RoleRegistry } from "./systemRoles";

export function registerRisaRole(registry: RoleRegistry): void {
  registry.registerRole({ kind: "risa", category: "feature", scope: "system", schema: z.object({}) });
}
