// risa's debug overlay flag and the app action that flips it: the risa twin of LSDj's (docs/modular-graph-phase-f4c
// design section 6.2). Off at start and not persisted, as the LSDj one is; the default backtick binding flips both.
import { createSignal } from "@retroplug/ui/menu";
import type { UiAction } from "@retroplug/ui/contribution";

export const risaDebug = createSignal(false);

export const risaDebugOverlay: UiAction = {
  id: "risa.debug-overlay",
  label: "risa Debug Overlay",
  keys: ["`"],
  run: () => risaDebug.set(!risaDebug.get()),
};
