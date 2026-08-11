// useN8SdWatch - keep the N8 Pro progress row live while a native SD job (ROM upload, SRAM
// dump/restore) runs. The worker updates its status on a background thread; nothing pushes into JS, so the
// UI polls. Every few frames it reads the cheap status snapshot and re-renders the caller ONLY when the
// job's version moves (a progress tick, phase change, done, or error) - so the steady state (idle, or menu
// closed) costs one hook call every POLL_FRAMES and never re-renders. Inert (null status) on a host without
// the N8 SD seam (the headless harness).

import { useRef, useState } from "react";

import { getN8SdStatus } from "../screens/menu/n8SdOps";
import { useNativeEvent } from "./useNativeEvent";

// ~10 Hz at 60 fps - smooth enough for a progress bar and to catch "done" promptly, cheap enough to run
// forever (one small hook call, no re-render unless the version changed).
const POLL_FRAMES = 6;

export function useN8SdWatch(): void {
  const [, bump] = useState(0);
  const ticks = useRef(0);
  const seenVersion = useRef(-1);

  useNativeEvent("frame", () => {
    if (++ticks.current < POLL_FRAMES) return;
    ticks.current = 0;
    const s = getN8SdStatus();
    if (!s) return;
    if (s.version !== seenVersion.current) {
      seenVersion.current = s.version;
      bump((n) => n + 1);
    }
  });
}
