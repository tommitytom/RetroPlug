// A scenario the two engines run differently: a computed "__proto__" key, which the ES5 lowering makes an assignment.
// Node sets the prototype (so the key is not the object's own and prints nothing); MicroQuickJS makes it an own key.
// The transcripts differ, which the toolchain must refuse at the line where they part
// (test/graph/device-pack.test.ts).
import { payload } from "./ts/core";

console.log("payload " + JSON.stringify(payload()));
