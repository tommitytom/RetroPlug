// FocusProvider — owns the app-wide "sink" keyboard group and hands it down via context.
//
// lv_binding_js's setKeyboardGroup(null) falls back to lv_group_get_default(), which holds every
// clickable widget (all our tiles), so LVGL would route arrows/Enter/Tab into them. The fix is an empty
// "sink" group that the keypad points at whenever nothing else claims it, leaving keys for our JS
// handlers with no LVGL side-effects. A focus surface (the Menu, via useFocusGroup) claims the keypad on
// mount and restores THIS sink on unmount — never null.
//
// This provider only owns the sink's LIFECYCLE (create / provide / destroy). It deliberately does NOT
// claim the keypad itself: a parent effect runs AFTER its children's, so claiming here would clobber a
// child menu that just claimed the keypad in the same commit. Idle claiming (keypad → sink when no menu
// is open) is state-driven in the menu controller.

import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import { createGroup, type Group } from "lvgljs";

const SinkGroupContext = createContext<Group | null>(null);

/** The app-wide sink group (null until FocusProvider's mount effect creates it). useFocusGroup restores
 *  the keypad to this on cleanup; the menu controller claims it while idle. */
export function useSinkGroup(): Group | null {
  return useContext(SinkGroupContext);
}

export function FocusProvider({ children }: { children: ReactNode }) {
  const [sink, setSink] = useState<Group | null>(null);

  useEffect(() => {
    const g = createGroup();
    setSink(g);
    return () => {
      g.destroy();
      setSink(null);
    };
  }, []);

  return <SinkGroupContext.Provider value={sink}>{children}</SinkGroupContext.Provider>;
}
