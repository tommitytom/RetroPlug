// The "bliptoaster" feature role. A control-plane-only marker role (no DSP behaviour, no config) attached to
// a BlipToaster ROM by the bliptoaster ROM provider (romProviders.ts). Its presence gates the BlipToaster asset
// menu — it is the tracker integration's markerRole — exactly as "risa" gates risa's menu. Registered into
// the control-plane registry only (buildAppRegistry), like registerRisaRole.
import { z } from "zod";
import type { RoleRegistry } from "./systemRoles";

export function registerBlipToasterRole(registry: RoleRegistry): void {
  registry.registerRole({
    kind: "bliptoaster",
    category: "feature",
    scope: "system",
    // `chip` is which expansion build this ROM is, set by the provider from the iNES mapper. It picks
    // the DAW parameter map's CC set (parameterMap.ts), which differs per build. A plain string rather
    // than an enum so a project written by a newer bundle (a chip this build has never heard of) still
    // parses; the lookup falls back to the 2A03 core. Additive with a default, so no migration step -
    // a project saved before this field existed reads as "2a03" until its ROM is re-detected.
    schema: z.object({ chip: z.string().default("2a03") }),
  });
}
