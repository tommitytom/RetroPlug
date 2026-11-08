// A scenario that walks only part of its package's core: the module it prints from is fine, and the module it never
// imports (ts/core.ts, whose launchable() is the unexercised Number.isInteger) must still be refused by the toolchain's
// whole-core scan (test/graph/device-pack.test.ts).
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
