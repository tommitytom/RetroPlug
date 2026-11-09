// A scenario over the portable core of a package with a UI half: the half (ts/ui, the directory the manifest's `ui`
// member's index names) is skipped by the whole-core scan, so a menu module's desktop-only calls do not fail a pack that
// has no menu on the engine. Every module outside that directory is still scanned, which the core-missing-call and
// app-module-sibling fixtures prove.
import { whole } from "./ts/core";

console.log("whole 4: " + whole(4));
