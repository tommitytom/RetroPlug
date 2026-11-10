// The LSDj debug overlay's visibility flag and the app action that flips it (docs/modular-graph-phase-f4c-design
// section 6.1). The overlay is a developer debugging aid, so it is OFF at start and deliberately NOT persisted: it is
// an ephemeral switch, so it carries no user-config or migration surface and resets each session.
//
// The flag is the package's own Signal; the shell watches it (a tile view's `visible`) and flips it through the action,
// which the binding profiles carry like any other. One press with the defaults flips this and risa's, so the backtick
// still shows both overlays.
import { createSignal } from "@retroplug/ui/menu";
import type { UiAction } from "@retroplug/ui/contribution";

export const lsdjDebug = createSignal(false);

export const lsdjDebugOverlay: UiAction = {
  id: "lsdj.debug-overlay",
  label: "LSDj Debug Overlay",
  keys: ["`"],
  run: () => lsdjDebug.set(!lsdjDebug.get()),
};
