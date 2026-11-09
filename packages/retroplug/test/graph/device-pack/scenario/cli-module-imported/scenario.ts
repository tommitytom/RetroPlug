// A scenario of a package whose core imports its cli half: a tool takes paths and runs on the host, so it is no part of
// a node's bundle, and the toolchain must refuse the import by name (test/graph/device-pack.test.ts).
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
