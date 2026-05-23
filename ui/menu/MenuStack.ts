import { useCallback, useState } from "react";

// Back-stack hook for hierarchical menus. `push(id)` enters a submenu;
// `pop()` returns to the parent. `reset()` collapses back to the root —
// used when the user switches focus to a different tile while the menu
// is open (we re-anchor and start at the root pane of the new instance).
//
// The stack stores pane ids only — pane contents are computed by the
// caller from menuDefs given the current id.
export function useMenuStack(rootId: string) {
    const [stack, setStack] = useState<string[]>([rootId]);

    const push = useCallback((id: string) => {
        setStack((s) => [...s, id]);
    }, []);

    const pop = useCallback(() => {
        setStack((s) => (s.length > 1 ? s.slice(0, -1) : s));
    }, []);

    const reset = useCallback(() => {
        setStack([rootId]);
    }, [rootId]);

    const currentId = stack[stack.length - 1];
    const canPop    = stack.length > 1;

    return { currentId, canPop, push, pop, reset };
}
