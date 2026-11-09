// A scenario of a package whose core imports a helper BESIDE the ui module's index: the index marks its whole directory
// as control-plane code, so the toolchain must refuse the import by name (test/graph/device-pack.test.ts).
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
