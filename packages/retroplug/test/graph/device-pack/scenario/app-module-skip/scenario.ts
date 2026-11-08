// A scenario over the portable core of a package that HAS a control-plane half: the half (ts/app.ts, the manifest's
// `app` module) is skipped by the whole-core scan, while every other module is scanned. Pointing the manifest at
// ts/core.ts instead is how the other half of the rule is proved: the same call, in a module that is not the app's, is
// refused (test/graph/device-pack/scenario/core-missing-call, which has no `app` member at all).
import { whole } from "./ts/core";

console.log("whole 3: " + whole(3));
