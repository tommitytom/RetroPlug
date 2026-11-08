// The scenario of a package whose control-plane half IMPORTS a module that calls what the engine lacks: the app module
// is skipped, the module it imports is not, so the toolchain refuses the pack.
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
