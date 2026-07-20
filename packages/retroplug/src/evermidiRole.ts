// The "evermidi" feature role. A control-plane-only marker role (no DSP behaviour, no config) attached to
// an EverMIDI ROM by the evermidi ROM provider (romProviders.ts). Its presence gates the EverMIDI asset
// menu — it is the tracker integration's markerRole — exactly as "risa" gates risa's menu. Registered into
// the control-plane registry only (buildAppRegistry), like registerRisaRole.
import { z } from "zod";
import type { RoleRegistry } from "./systemRoles";

export function registerEverMidiRole(registry: RoleRegistry): void {
  registry.registerRole({ kind: "evermidi", category: "feature", scope: "system", schema: z.object({}) });
}
