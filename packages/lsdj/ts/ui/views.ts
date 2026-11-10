// LSDj's views (docs/modular-graph-phase-f5a-design.md section 5.3), apart from its UI contribution: the HD player's
// full-window screen and the debug readout over every tile. They are a manifest member of their own (`retroplug.views`)
// because only the UI bundle may carry a package's views - the pure tier bundles the menu modules with no alias, so a
// module that imports React would break every menu test - and neither view imports anything of the shell's.
import { defineViews } from "@retroplug/ui/contribution";
import { hdView } from "./hdView";
import { overlayView } from "./overlayView";

export default defineViews({ views: [hdView, overlayView] });
