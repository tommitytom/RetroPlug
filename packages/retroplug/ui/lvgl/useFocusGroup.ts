// useFocusGroup — claim the LVGL keyboard group for a focus surface (a menu, a modal).
//
// The reusable form of the createGroup / add-refs / setKeyboardGroup / restore-the-sink dance the legacy
// UI copy-pastes across Menu / KitEditor / AboutPanel. On mount (and whenever `deps` change) it builds a
// fresh group from the surface's ordered refs, focuses a target, and points the keypad at it; on cleanup
// it restores the app sink group (from FocusProvider) and destroys the group.
//
// useLayoutEffect (not useEffect) so the claim/restore runs in the commit's mutation phase: the lvgljs
// reconciler schedules passive effects via queueMicrotask, and LVGL's paint timers can fire in between —
// a passive claim would blip a frame, and a passive restore could run AFTER a sibling that re-mounted in
// the same commit already claimed the keypad, clobbering it.
//
// The legacy stray-focus guard (isRebuildingRef) is intentionally NOT here: the greenfield menu styles
// its highlight with LVGL's native onFocusedStyle instead of a React cursor fed by onFocus, so there's
// no state for stray FOCUSED events to corrupt. Re-add it as an opt-in only if a real flicker appears.

import { useCallback, useLayoutEffect, useRef } from "react";
import { createGroup, setKeyboardGroup, type Group } from "lvgljs";

import { useSinkGroup } from "./FocusProvider";

export interface FocusGroupOptions {
  /** Rebuild the group when any of these change (e.g. the menu's visible-item key). */
  deps: unknown[];
  /** Which ref to focus after (re)building, given the ordered refs. Defaults to the first. */
  focusTarget?: (refs: unknown[]) => unknown;
}

/** Returns a stable `focus(ref)` so the surface can move keypad focus (arrow nav) within its group. */
export function useFocusGroup(getRefs: () => unknown[], { deps, focusTarget }: FocusGroupOptions): (ref: unknown) => void {
  const sink = useSinkGroup();
  const groupRef = useRef<Group | null>(null);

  useLayoutEffect(() => {
    const group = createGroup();
    groupRef.current = group;
    const refs = getRefs().filter((r) => r != null);
    for (const ref of refs) group.add(ref);
    const target = focusTarget?.(refs) ?? refs[0];
    if (target) group.focus(target);
    setKeyboardGroup(group);
    return () => {
      setKeyboardGroup(sink ?? null);
      group.destroy();
      groupRef.current = null;
    };
    // Rebuild is keyed on `deps` (+ the sink), not on the getRefs/focusTarget closures.
  }, [sink, ...deps]);

  return useCallback((ref: unknown) => {
    if (ref) groupRef.current?.focus(ref);
  }, []);
}
