// The blastem package's app contribution: the `blastem` system role, keyed by the core (platform.ts's Core value), so
// it attaches to any BlastEm system the way `mesen` does to Mesen's. Its schema is the console's knobs; `region` is the
// one the node reads (as a construct-time field, so the store rebuilds on a change).
import type { RoleType } from "@retroplug/core/systemRoles";
import { definePackage } from "@retroplug/core/definePackage";
import { enumField } from "@retroplug/core/configFields";
import { z } from "zod";
import { MD_REGION_VALUES } from "./enums";

export const BLASTEM_ROLE: RoleType = {
  kind: "blastem",
  category: "system",
  // enumField, not z.enum(...).default(...): every role schema CLAMPS a junk value to its default rather than throwing
  // (core's configFields), so a project written by a newer build still parses.
  schema: z.object({ region: enumField(MD_REGION_VALUES, "auto") }),
};

export default definePackage({ roles: [BLASTEM_ROLE] });
