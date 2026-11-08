// A scenario importing a package its manifest does not declare: the toolchain must refuse it, naming the module and
// the importer (test/graph/device-pack.test.ts).
import { greet } from "./ts/core";
import { PRO_MK3 } from "@retroplug/launchpad";

console.log(greet() + " " + PRO_MK3.name);
