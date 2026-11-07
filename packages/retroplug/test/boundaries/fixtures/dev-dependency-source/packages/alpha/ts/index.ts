// A devDependency is a test file's: a source file that imports one is refused, as a native library target that links one
// is.
import { harness } from "@retroplug/testing";

export const alpha = harness;
