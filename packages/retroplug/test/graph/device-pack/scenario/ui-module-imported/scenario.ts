// A scenario of a package whose CORE imports its menu half through the directory its `ui` member's index marks as
// control-plane code: the toolchain must refuse the import by name (test/graph/device-pack.test.ts). Without the check
// the whole-core scan would skip the menu's own call and the run would be refused by whatever it happened to reach.
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
