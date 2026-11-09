import { systemRoleFor } from "@retroplug/core/systemRoles";
import { ROLES } from "./roles";

export const roles = [systemRoleFor("sameboy"), ...ROLES];
