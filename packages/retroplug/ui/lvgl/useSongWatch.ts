// useSongWatch - notice when the focused cart's loaded song changes, and record it in the Recent list.
//
// A song changes two ways: the tracker submenu's Songs > Load (which records immediately itself), and the
// user loading one from INSIDE LSDj / risa, which nothing else in the app can see. Both land in the live
// battery, so one poll of it covers both: every ~POLL_FRAMES render frames, ask the project store to record
// whatever song the focused cart currently has. RecentStore no-ops (no write, no notify) while the answer
// is unchanged, so the steady state costs one SRAM snapshot read + a header-only name parse.
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
    stores.project.recordCurrentSong();
    // Same rhythm, same battery, different question: has the SONG ITSELF changed under a control surface?
    // A cart being edited on its own screen tells the app nothing, so this is the only way the Launchpad's
    // grid learns that row 12 just gained a chain. Inert unless a controller is enabled.
    stores.project.refreshControllerSong();
  });
}
