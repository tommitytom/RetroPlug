// A process-wide toggle for the LSDj runtime overlay (LsdjOverlay). The overlay is a developer debugging
// aid — a live WRAM readout — not something to show players, so it is OFF by default and flipped by a
// keyboard shortcut (backtick, wired in App). Deliberately NOT persisted: it's an ephemeral debug switch,
// so it carries no user-config / migration surface and resets to off each session.
//
// A tiny external store (not React context) so the shortcut can live in App while every mounted overlay
// re-renders on toggle via useSyncExternalStore, without threading a prop through the grid.
import { useSyncExternalStore } from "react";

let visible = false;
const listeners = new Set<() => void>();

/** Flip the overlay on/off (called from App's key handler). Notifies every mounted overlay. */
export function toggleLsdjDebug(): void {
  visible = !visible;
  for (const l of listeners) l();
}

/** Whether the LSDj debug overlay is currently shown. Reactive — a component reading this re-renders on toggle. */
export function useLsdjDebugVisible(): boolean {
  return useSyncExternalStore(
    (cb) => {
      listeners.add(cb);
      return () => listeners.delete(cb);
    },
    () => visible,
  );
}
