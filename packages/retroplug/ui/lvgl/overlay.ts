// The shape every full-window overlay hook returns (the file browser, the close guard, the project modals,
// the song import). App keeps them in one priority-ordered list and lets the first live one own the window
// and the input - so "is an overlay up", "what does Esc cancel" and "what is drawn" are each answered once,
// rather than by a hand-written chain per call site that can (and did) forget one of them.

import type { MenuTree } from "../screens/menu/menuTree";

export interface Overlay {
  /** True while the overlay is up; `tree` is non-null exactly then. */
  active: boolean;
  /** The menu tree to draw full-window, or null. */
  tree: MenuTree | null;
  /** Esc / B: dismiss (each overlay decides what cancelling means). */
  onClose: () => void;
}

/** The overlay that owns the window: the first active one in priority order, or null. */
export function topOverlay(overlays: readonly Overlay[]): Overlay | null {
  return overlays.find((o) => o.active && o.tree !== null) ?? null;
}
