// useGameInput — route keyboard into the focused emulator's joypad.
//
// Keys already reach the UI on the "key" bus (the editor's onKeyboard emits [dpfCode, press]); this hook
// resolves each code to a Game Boy button via the active bindings and fires backend.pressButton at the
// focused system. It coexists with App's Esc handler (a separate "key" subscription): Esc isn't bound to
// a button, so it falls through here untouched. Repeat-drop and release-to-original-target are
// useHeldRouting's, keyed by the dpf code.

import { useMemo } from "react";

import { useBindings } from "../stores/useStores";
import { useNativeEvent } from "../lvgl/useNativeEvent";
import { buildKeyToButton } from "../../src/keyCodes";
import { useHeldRouting } from "./useHeldInput";

export function useGameInput({ active, focusedId }: { active: boolean; focusedId: number }) {
  const bindings = useBindings();
  const keyToButton = useMemo(() => buildKeyToButton(bindings.keyboard), [bindings.keyboard]);
  const routing = useHeldRouting(active, focusedId);

  useNativeEvent("key", (key, press) => {
    const button = keyToButton.get(key);
    if (button === undefined) return; // not a game key (e.g. Esc → App's menu handler)
    if (press) routing.press(`key:${key}`, button);
    else routing.release(`key:${key}`, button);
  });
}
