// A scenario whose own imports are all inside its allowlist, while a module of the package's core that it never reaches
// imports a package the manifest does not declare: the core's own allowlist must refuse it
// (test/graph/device-pack.test.ts).
import { core } from "./ts/core";

console.log("core: " + core());
