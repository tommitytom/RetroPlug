// useSramAutoSave - drive the Continuous SRAM auto-save preference.
//
// Without this the "Continuous" setting is a promise the app never keeps: SramAutoSaver.pump() had no
// caller anywhere, so a user who picked it (believing their battery was being mirrored to disk as they
// worked) got exactly the same protection as "Off". The setting is offered in Settings and persisted in
// the user config, so it has to actually run.
//
// Same shape as useSongWatch: a frame-tick counter, one long-lived saver in a ref. pump() self-gates on
// the preference, so Off / OnProjectSave cost one enum read per poll and nothing else.
//
// Editor-driven, like the file watcher and the song watch: a DAW instance whose editor is closed mirrors
// nothing until it's opened again. (The battery still reaches disk on project save via flushDirtySram.)

import { useRef } from "react";

import type { AppStores } from "../../src/appStores";
import { SramAutoSaver } from "../../src/sramAutoSave";
import { useNativeEvent } from "./useNativeEvent";

// ~2 s at 60 fps. Deliberately slower than useSongWatch's 0.5 s: that polls a header-only name parse,
// whereas a battery check can reach sramSignature's full encodeSong(decodeSong(...)) round-trip. The raw
// hash gate in SramAutoSaver keeps the steady state to one whole-battery FNV, and 2 s is a small window
// of exposure against a crash while staying invisible in a profile.
const POLL_FRAMES = 120;

export function useSramAutoSave(stores: AppStores): void {
  const ticks = useRef(0);
  const saver = useRef<SramAutoSaver | null>(null);
  saver.current ??= new SramAutoSaver(stores.backend, stores.project.systems, stores.userConfig);

  useNativeEvent("frame", () => {
    if (++ticks.current < POLL_FRAMES) return;
    ticks.current = 0;
    // One system per tick: bounded per-frame work, so a multi-cart project can't stall a frame.
    saver.current!.pump(1);
  });
}
