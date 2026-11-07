// A package that reaches the shell for TYPES alone: an `import type`, an `export type ... from` and a type-position
// `import("x").T` are all judged, because the boundary is what the package needs to compile on its own, not what a
// bundler keeps.
import type { A } from "../../retroplug/src/a";
export type { B } from "../../retroplug/src/b";

export const c: import("../../retroplug/src/c").C = {};

export type UsesA = A;
