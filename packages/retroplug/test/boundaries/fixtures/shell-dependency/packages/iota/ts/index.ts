// A package that imports the shell, the half of the rule its manifest breaks too: no package may depend on the app.
import { shell } from "@retroplug/retroplug";

export const iota = shell;
