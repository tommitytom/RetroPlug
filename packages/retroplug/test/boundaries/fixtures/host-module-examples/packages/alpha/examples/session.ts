// A host module imported by an example session (not by ts/), which the package does not declare: the walk sees
// examples/ too, because the shell's program compiles it.
import { fixture } from "fixture-lib";

export const session = fixture;
