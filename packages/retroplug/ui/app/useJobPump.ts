// useJobPump - settle capability jobs (src/capability.ts) and offline jobs (src/offline.ts) as they complete.
//
// A job's promise settles when the backend drains its completion. Everything that completes at once (a read of an
// emulated system, a write while no audio thread runs) settles at its submission; a write while the audio thread runs
// completes after the next block, a device's job seconds later, and an offline job when its worker finishes it, so
// something has to look again. That is this
// hook, on the render frame the other watchers already count, rather than a timer of its own: pumpJobs makes no native
// call while nothing is pending, so the steady state costs one size check a frame.
//
// Editor-driven, like the other watchers: a DAW instance whose editor is closed settles its pending jobs when it is
// opened again, or at the next submission.

import type { AppStores } from "../../src/appStores";
import { useFrameDivider } from "../lvgl/useFramePoll";

export function useJobPump(stores: AppStores): void {
  useFrameDivider(1, () => {
    stores.backend.pumpJobs();
  });
}
