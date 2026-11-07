// A package that imports another by its package name without declaring it: the boundary check refuses the import,
// not the resolution (esbuild leaves a bare specifier alone, so the fixture needs no node_modules).
import { beta } from "@retroplug/beta";

export const alpha = beta;
