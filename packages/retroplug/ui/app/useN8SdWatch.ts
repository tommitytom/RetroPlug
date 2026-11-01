// useN8SdWatch - keep the N8 Pro card-operation row live while a job (ROM upload, battery back up or restore) runs.
// The job is a graph capability promise with a progress callback, so the snapshot moves in JS; this poll re-renders
// the caller ONLY when its version moves (a progress tick, done, or an error) - so the steady state (idle, or menu
// closed) costs one cheap read every POLL_FRAMES and never re-renders.

import { getN8CartJob } from "../screens/menu/n8SdOps";
import { useVersionedPoll } from "../lvgl/useFramePoll";

// ~10 Hz at 60 fps - smooth enough for a progress bar and to catch "done" promptly, cheap enough to run
// forever (one small read, no re-render unless the version changed).
const POLL_FRAMES = 6;

export function useN8SdWatch(): void {
  useVersionedPoll(POLL_FRAMES, getN8CartJob);
}
