// risa's two config-free feature roles: the `risa` marker role, which gates the Songs and Assets menus
// (`sys.roles.find(r => r.kind === "risa")`, exactly as `lsdj-sync` gates the LSDj menu), and `risa-sync`, whose work
// is the rp.risa.sync protocol node (./risaSyncNode) that drives the cart's N8-FIFO host-sync receive path from the
// DAW transport. Both are control-plane registrations only (the package's app module, ts/app.ts); the ROM provider
// that attaches them by ROM identity is ./romProvider.
//
// The asset-override role is its own module (./risaAssetsRole), because it carries an onConstruct hook.
import { z } from "zod";
import type { RoleRegistry, RoleType } from "@retroplug/core/systemRoles";

/** The `risa` marker role: a risa cart's presence, and nothing else. */
export const RISA_ROLE: RoleType = { kind: "risa", category: "feature", schema: z.object({}) };

/** risa-sync: the DAW transport into a sync-capable risa cart's host-sync receive path. No config. */
export const RISA_SYNC_ROLE: RoleType = { kind: "risa-sync", category: "feature", schema: z.object({}) };

/** Register the marker role into `registry`, for a registry a test or a host builds by hand. */
export function registerRisaRole(registry: RoleRegistry): void {
  registry.registerRole(RISA_ROLE);
}
