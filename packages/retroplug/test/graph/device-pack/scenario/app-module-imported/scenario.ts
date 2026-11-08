// A scenario of a package whose CORE imports its control-plane half: the toolchain must refuse the import by name
// (test/graph/device-pack.test.ts). The whole-core scan skips the app module, so without the check the run is refused
// only by whatever the zod code inside it happens to call, which names neither module.
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
