// useLaunchpadScanWatch - keep the Settings > MIDI control-surface row live while a native port scan runs,
// and record what it found the moment it finishes. The scan runs on a background thread; nothing pushes into
// JS, so the UI polls - the useN8SdWatch shape exactly, re-rendering only when the scan's version moves (a
// port probed, done, or an error), so the steady state costs one hook call every POLL_FRAMES and no render.
// Inert (null status) on a host without the scan seam (a DAW-hosted editor, the headless harness).
//
// Applying the result HERE, rather than in the menu, is deliberate: a scan finishes whether or not the menu
// happens to be open, and its answer is what makes the instance menu's Launchpad submenu appear. A result
// that only landed while somebody was looking at the right row would be a scan you had to watch.

import { applyLaunchpadScan, getLaunchpadScan } from "../screens/menu/launchpadDevices";
import { useVersionedPoll } from "./useFramePoll";

// ~10 Hz at 60 fps - prompt enough to catch "done" and to animate the per-port phase, cheap enough to run
// forever.
const POLL_FRAMES = 6;

export function useLaunchpadScanWatch(): void {
  // The third argument is the finishing EDGE, latched by useVersionedPoll: polling sees "done" many
  // times over, and re-applying each time would keep rewriting launchpad.cfg - and stomp a port the user
  // picked by hand afterwards.
  useVersionedPoll(POLL_FRAMES, getLaunchpadScan, applyLaunchpadScan);
}
