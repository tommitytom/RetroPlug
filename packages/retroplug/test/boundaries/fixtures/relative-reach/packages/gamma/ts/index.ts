// A package that reaches into the shell by a relative path, which is how every cart reached core's TS half and the
// test harness before the split: the check refuses it whatever the file it names.
import { shell } from "../../retroplug/src/shell";

export const gamma = shell;
