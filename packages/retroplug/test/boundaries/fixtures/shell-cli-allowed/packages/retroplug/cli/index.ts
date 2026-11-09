// A contract module, and a relative reach into src: the CLI is a surface over the control plane, so both are its own.
import type { Host } from "@retroplug/core/host";
import { ROLES } from "../src/roles";

export const tools = [ROLES, null as Host | null];
