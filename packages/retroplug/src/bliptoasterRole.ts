// The "bliptoaster" feature role. A control-plane-only marker role (no DSP behaviour, no config) attached to
// a BlipToaster ROM by the bliptoaster ROM provider (romProviders.ts). Its presence gates the BlipToaster asset
// menu — it is the tracker integration's markerRole — exactly as "risa" gates risa's menu. Registered into
// the control-plane registry only (buildAppRegistry), like registerRisaRole.
import { z } from "zod";
import type { RoleRegistry } from "./systemRoles";

export function registerBlipToasterRole(registry: RoleRegistry): void {
  registry.registerRole({ kind: "bliptoaster", category: "feature", scope: "system", schema: z.object({}) });
}
