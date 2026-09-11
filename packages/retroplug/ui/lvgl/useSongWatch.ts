// useSongWatch - keep the Recent list in step with the focused cart.
//
// A song changes two ways: a song request (a Recent row or the Songs menu's Load, which records its own
// row when it settles), and the user loading one from INSIDE LSDj / risa / smsggdj, which nothing else in
// the app can see. Both land in the live cart, so one poll covers both: every ~POLL_FRAMES render frames,
// ask the project store to sync. The same poll pays a project row the store OWES - one it declined to
// record at load time because the cart could not yet say what it held (smsggdj boots for a few seconds
// before its work RAM is its own). RecentStore no-ops (no write, no notify) while the answer is
// unchanged, so the steady state costs one snapshot read + a header-only name parse.
//
// Editor-driven, like the file watcher: a DAW instance whose editor is closed records nothing until it's
// opened again.

import { useRef } from "react";

import type { AppStores } from "../../src/appStores";
import { useNativeEvent } from "./useNativeEvent";

// ~0.5 s at 60 fps. Fast enough that the row is there by the time the user reopens the menu, slow enough
// that the battery read never shows up in a profile.
const POLL_FRAMES = 30;

export function useSongWatch(stores: AppStores): void {
  const ticks = useRef(0);

  useNativeEvent("frame", () => {
    if (++ticks.current < POLL_FRAMES) return;
    ticks.current = 0;
    stores.project.syncRecent();
    // Same rhythm, same battery, different question: has the SONG ITSELF changed under a control surface?
    // A cart being edited on its own screen tells the app nothing, so this is the only way the Launchpad's
    // grid learns that row 12 just gained a chain. Inert unless a controller is enabled.
    stores.project.refreshControllerSong();
  });
}
