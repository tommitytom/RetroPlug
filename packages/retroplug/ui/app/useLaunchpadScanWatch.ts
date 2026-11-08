// useLaunchpadScanWatch - drive a control-surface port scan, keep the Settings > MIDI row live while it runs, and record
// what it found the moment it finishes. The scan is a TS state machine (PortScan), and this poll is what STEPS it: each
// tick reads what the inputs heard over the Launchpad facet and, once an output's answer window has passed, probes the
// next. Re-renders only when the scan's version moves (a port probed, a reply, done), so the steady state costs one hook
// call every POLL_FRAMES and no render. Inert (null status) on a host that mounts no facet (a DAW-hosted editor).
//
// Applying the result HERE, rather than in the menu, is deliberate: a scan finishes whether or not the menu happens to be
// open, and its answer is what makes the instance menu's Launchpad submenu appear. A result that only landed while
// somebody was looking at the right row would be a scan you had to watch.

import { applyLaunchpadScan, pollControllerRole, pollLaunchpadScan } from "../screens/menu/launchpadDevices";
import type { AppStores } from "../../src/appStores";
import { useFrameDivider, useVersionedPoll } from "../lvgl/useFramePoll";

// ~20 Hz at 60 fps. The answer windows are SCAN_WINDOW_MS (300 ms) long and measured off the wall clock, so this only
// sets how promptly a window that has elapsed is noticed; it costs nothing while no scan runs.
const POLL_FRAMES = 3;

export function useLaunchpadScanWatch(stores: AppStores): void {
  // The third argument is the finishing EDGE, latched by useVersionedPoll: polling sees "done" many times over, and
  // re-applying each time would keep rewriting config.json - and stomp a port the user picked by hand afterwards.
  useVersionedPoll(POLL_FRAMES, pollLaunchpadScan, (s) => applyLaunchpadScan(stores.userConfig, s));
  // And whether the open project still runs the controller: when it stops, the device is let go (see pollControllerRole)
  // rather than left locked in Programmer mode with nothing driving it.
  useFrameDivider(POLL_FRAMES, () => pollControllerRole(stores.project.settings().controller.enabled));
}
