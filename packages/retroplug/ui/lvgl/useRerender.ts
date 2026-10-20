// Forcing a re-render, for state that lives OUTSIDE React (a module-level request, a value in the native
// host) and so has no React path to tell the tree it moved.

import { useCallback, useEffect, useState } from "react";

/** A stable function that re-renders the calling component. */
export function useForceUpdate(): () => void {
  const [, bump] = useState(0);
  return useCallback(() => bump((n) => n + 1), []);
}

/** Re-render whenever any of `sources` fires. Each source is a `subscribe(listener) => unsubscribe` function
 *  (createSubscribable's shape) and must be stable - a module-level export, not an inline closure. Returns
 *  the force-update too, for a caller that also moves the external state itself. */
export function useRerenderOn(...sources: ((listener: () => void) => () => void)[]): () => void {
  const forceUpdate = useForceUpdate();
  useEffect(() => {
    const offs = sources.map((subscribe) => subscribe(forceUpdate));
    return () => offs.forEach((off) => off());
    // Keyed on the sources themselves: stable module functions, so this runs once per mount.
  }, [forceUpdate, ...sources]);
  return forceUpdate;
}
