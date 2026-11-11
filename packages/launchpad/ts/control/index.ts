// The launchpad pack's app module (docs/modular-graph-phase-f5a-design.md sections 6.1 and 6.2): what it contributes to
// the app itself rather than to a surface. Three things, all the controller's:
//
//   projectSections  the project's `controller` block: the schema it is parsed with, its defaults and how a patch lands
//                    on it. A project that carries a controller block keeps it where it was, and a product without this
//                    pack keeps the block verbatim (no contributor), so nothing is lost either way.
//   projectServices  the control-plane half: the poll of the cart the surface drives, and the model the graph places.
//                    The shell builds one instance per project store, so a DAW's two instances share nothing.
//
// The graph placement itself is `protocol.placer` (ts/control/placer.ts), a module of its own because the planner's
// bundle may carry no node code, and the menus are `retroplug.ui` (ts/ui/index.ts).
//
// This file is the manifest's `retroplug.app` module, and its directory is control plane rather than portable core: the
// device pack's scanner leaves out the directory of an `index` entry, so a sibling of this file (a project section's
// schema, a service's poll, a placer) is never part of the package's MicroQuickJS core, which is what keeps the
// scenario test's byte-for-byte guarantee. A package with its own app module puts its control-plane modules beside its
// index the same way.
import { definePackage } from "@retroplug/core/definePackage";
import type {} from "@retroplug/tracker/packageContributions";
import { controlSurfaceSection } from "./configSection";
import { controllerSection } from "./projectSection";
import { controllerService } from "./projectService";

export default definePackage({
  projectSections: [controllerSection],
  projectServices: [controllerService],
  // The machine's control surface (docs/modular-graph-phase-f5b-design.md section 6.1): a root key of config.json
  // whose shape the pack owns, so the shell names no port, field or default. The key and its fields are the ones the
  // file always carried, so an existing file's bytes do not move.
  configSections: [controlSurfaceSection],
});
