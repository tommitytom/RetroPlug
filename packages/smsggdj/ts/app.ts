// smsggdj's contribution to the app: the `sms-sync` role whose work is rp.sms.sync, the ROM provider that attaches it
// (by the cart's build marker, not by platform) and its Songs/Assets tracker integration. The role's schema is what the
// stores parse, persist and hand the node as its settings; `machine` is additive with a default, so no migration step:
// an existing project stamped before it existed parses as "sms", which is what those projects were.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType } from "@retroplug/core/systemRoles";
import { z } from "zod";
import { SMSGGDJ_ROM_PROVIDER } from "./romProvider";
import { smsggdjIntegration } from "./smsggdjIntegration";
// The key the tracker package adds to the contract (its module augments PackageContributions): type-only, so nothing
// loads at run time.
import type {} from "@retroplug/tracker/packageContributions";

// sms-sync: clocks smsggdj by holding a 2-bit counter on controller port 2 (TR + TH) at 24 PPQN, or a Game Gear's EXT
// port for `machine: "gg"`. `rp.sms.sync` runs it (./smsSyncNode).
export const SMS_SYNC_ROLE: RoleType = {
  kind: "sms-sync",
  category: "feature",
  schema: z.object({ machine: z.enum(["sms", "gg"]).default("sms") }),
};

export default definePackage({
  roles: [SMS_SYNC_ROLE],
  romProviders: [SMSGGDJ_ROM_PROVIDER],
  trackerIntegrations: [smsggdjIntegration],
});
