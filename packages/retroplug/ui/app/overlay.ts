// The shape every full-window overlay hook returns (the file browser, the close guard, the project modals,
// the song import). App keeps them in one priority-ordered list and lets the first live one own the window
// and the input - so "is an overlay up", "what does Esc cancel" and "what is drawn" are each answered once,
// rather than by a hand-written chain per call site that can (and did) forget one of them.

import type { MenuTree } from "../screens/menu/menuTree";

export interface Overlay {
  /** The menu tree to draw full-window while the overlay is up; null when it isn't. The ONLY "is it up"
   *  signal - a separate flag could disagree with it, and an overlay that was up with nothing to draw
   *  would then be skipped for rendering, Esc and input gating all at once. */
  tree: MenuTree | null;
  /** Esc / B: dismiss (each overlay decides what cancelling means). */
  onClose: () => void;
}

/** An overlay that is up, its tree narrowed to non-null. */
export type LiveOverlay = Overlay & { tree: MenuTree };

/** The overlay that owns the window: the first live one in priority order, or null. */
export function topOverlay(overlays: readonly Overlay[]): LiveOverlay | null {
  return overlays.find((o): o is LiveOverlay => o.tree !== null) ?? null;
}
