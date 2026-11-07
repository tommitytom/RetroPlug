// A package that imports zod without declaring it: a third-party module is a dependency like a package is, and a
// package carries only what its manifest says it does.
import { z } from "zod";

export const delta = z.string();
