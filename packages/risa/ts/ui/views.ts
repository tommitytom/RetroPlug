// risa's view (docs/modular-graph-phase-f5a-design.md section 5.3), apart from its UI contribution: the debug readout
// over every tile. A manifest member of its own (`retroplug.views`) because only the UI bundle may carry a package's
// views - the pure tier bundles the menu modules with no alias, so a module that imports React would break every menu
// test - and the readout imports nothing of the shell's.
import { defineViews } from "@retroplug/ui/contribution";
import { overlayView } from "./overlayView";

export default defineViews({ views: [overlayView] });
