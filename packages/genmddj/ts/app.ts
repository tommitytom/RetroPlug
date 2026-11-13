// genmddj's app contribution: the `genmddj-sync` role whose work is rp.genmddj.sync, and the ROM provider that attaches
// it by the cart's identity. The role's schema has no fields yet (spec 2 adds the tracker integration); an empty object
// is additive, so a project written before it parses as {}.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType } from "@retroplug/core/systemRoles";
import { z } from "zod";
import { GENMDDJ_ROM_PROVIDER } from "./romProvider";

// genmddj-sync: clocks genmddj by holding a 2-bit counter on controller port 2's TR and TH at 24 PPQN
// (`rp.genmddj.sync`, ./genmddjSyncNode). The provider attaches it only to a genmddj cart, and the system opens that
// port only for a system that places the role: every other Mega Drive game reads player 2.
export const GENMDDJ_SYNC_ROLE: RoleType = {
  kind: "genmddj-sync",
  category: "feature",
  schema: z.object({}),
};

export default definePackage({ roles: [GENMDDJ_SYNC_ROLE], romProviders: [GENMDDJ_ROM_PROVIDER] });
