// useStateRef: React state mirrored into a ref, for state a long-lived handler must read synchronously.
//
// The native bus handlers (useNativeEvent) and LVGL's key/click callbacks run between renders: a key that
// arms a capture row and the next key that it captures can arrive inside one event dispatch, before React
// has re-rendered with the new state. Those handlers read the REF (current the moment `set` returns); the
// render reads the state. The setter writes both, so they can never disagree.

import { useCallback, useRef, useState } from "react";

/** [state for rendering, ref for handlers, setter that updates both]. The setter takes a value, not an
 *  updater function: derive the next value from `ref.current`. */
export function useStateRef<T>(initial: T | (() => T)): [T, { readonly current: T }, (next: T) => void] {
  const [state, setState] = useState(initial);
  const ref = useRef(state);
  const set = useCallback((next: T) => {
    ref.current = next;
    setState(next);
  }, []);
  return [state, ref, set];
}
