// Frame-divided polling, which the four watcher hooks in ui/app/ were each doing by hand.
//
// Several pieces of state live on a background thread natively and push nothing into JS - an N8 SD job,
// a Launchpad port scan, the song inside a running cart, an unsaved battery. The only way the UI learns
// about them is to look, and the only clock available is the render frame. So each hook counts frames
// and acts on every Nth.
//
// Three shapes:
//
//   useFrameDivider  - "do this every N frames". No re-render of its own; the caller's work is a store
//                      call whose own notification drives whatever needs repainting.
//   useVersionedPoll - "watch this native status and re-render when it MOVES". The status carries a
//                      version, and re-rendering on every tick instead would repaint the menu ~10x a
//                      second forever, for a value that changes a handful of times per job.
//   useFrameSampled  - "sample this EVERY frame and re-render only when what's SHOWN changes". For a live
//                      readout (the LSDj / risa debug overlays, the render badge): the sample is cheap and
//                      changes often, so a caller-supplied signature of exactly the displayed fields is the
//                      dedupe, rather than a version the native side would have to keep.
import { useEffect, useRef, useState } from "react";

import { useNativeEvent } from "./useNativeEvent";
import { useForceUpdate } from "../hooks/useRerender";

/** Run `fn` on every `frames`-th render frame. The counter is per-component, not global. */
export function useFrameDivider(frames: number, fn: () => void): void {
  const ticks = useRef(0);
  useNativeEvent("frame", () => {
    if (++ticks.current < frames) return;
    ticks.current = 0;
    fn();
  });
}

/**
 * Poll a versioned native status every `frames` frames and re-render the caller only when its version
 * changes. A null status means the host has no such seam (a DAW-hosted editor, the headless harness) and
 * is simply skipped.
 *
 * `onSettled` fires on the finishing EDGE, once per job, and exists for the Launchpad scan: polling sees
 * "done" over and over, and re-applying each time would keep rewriting launchpad.cfg and stomp a port the
 * user picked by hand afterwards. `busy` is what re-arms it, so a second run is applied again.
 */
export function useVersionedPoll<T extends { version: number; busy?: boolean; done?: boolean }>(
  frames: number,
  read: () => T | null,
  onSettled?: (status: T) => void,
): void {
  const rerender = useForceUpdate();
  const seenVersion = useRef(-1);
  const settled = useRef(false);

  useFrameDivider(frames, () => {
    const s = read();
    if (!s) return;
    if (onSettled) {
      if (s.busy) settled.current = false; // a fresh run: its result has not been taken yet
      if (s.done && !s.busy && !settled.current) {
        settled.current = true;
        onSettled(s);
      }
    }
    if (s.version !== seenVersion.current) {
      seenVersion.current = s.version;
      rerender();
    }
  });
}

/**
 * Sample `sample` on every frame and hold the result as state, re-rendering only when `signature` of the new
 * sample differs from the last one. `sample` returns `undefined` to skip a frame (nothing readable - keep
 * what's shown); any other value, `null` included, is a sample. The readout starts at null with an empty
 * signature, so a signature of "" for the idle value costs no render at all.
 *
 * `sample` must be MEMOISED (useMemo / useCallback): a new identity means a new source - the cart was swapped,
 * or the readout was switched off - and resets the readout to null. Pass null to switch sampling off.
 */
export function useFrameSampled<T>(sample: (() => T | undefined) | null, signature: (value: T) => string): T | null {
  const [state, setState] = useState<T | null>(null);
  const seen = useRef("");

  useEffect(() => {
    setState(null);
    seen.current = "";
  }, [sample]);

  useNativeEvent("frame", () => {
    if (!sample) return;
    const next = sample();
    if (next === undefined) return;
    const sig = signature(next);
    if (sig === seen.current) return; // nothing shown changed → no re-render
    seen.current = sig;
    setState(next);
  });

  return sample ? state : null;
}
