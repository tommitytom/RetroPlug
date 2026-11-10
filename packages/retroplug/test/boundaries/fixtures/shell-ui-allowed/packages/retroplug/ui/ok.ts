// A UI file the rule admits: a contract module from the UI's own list, and a relative reach into the shell's src (the
// UI is a surface over the stores the control plane owns).
import { action } from "@retroplug/ui/menu";
import { ROLES } from "../src/roles";

export const row = [action("ok", "OK", () => {}), ROLES];
