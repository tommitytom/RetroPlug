// useGameInput — route keyboard into the focused emulator's joypad.
//
// Keys already reach the UI on the "key" bus (the editor's onKeyboard emits [dpfCode, press]); this hook
// resolves each code to a Game Boy button via the active bindings and fires backend.pressButton at the
// focused system. It coexists with App's Esc handler (a separate "key" subscription): Esc isn't bound to
// a button, so it falls through here untouched.
//
// The `targets` map (dpf code → system id) does the same double duty as the legacy shell's keyTargetRef:
//   - OS auto-repeat drop — every platform streams press=true while a key is held; the GB joypad only
//     needs one transition per physical press, so a code already in the map is ignored.
//   - release-to-original-target — the release goes to the system the press was routed to, even if focus
//     moved mid-hold, so a held button never sticks on the wrong (or a gone) instance.
// Releases are always processed (even when inactive) so opening a menu mid-hold can't strand a key down.

import { useMemo, useRef } from "react";

import { useStores, useBindings } from "../stores/useStores";
import { useNativeEvent } from "../lvgl/useNativeEvent";
import { buildKeyToButton } from "../../src/keyCodes";

export function useGameInput({ active, focusedId }: { active: boolean; focusedId: number }) {
  const { backend } = useStores();
  const bindings = useBindings();
  const keyToButton = useMemo(() => buildKeyToButton(bindings.keyboard), [bindings.keyboard]);
  // dpf code → the system id its press was routed to (also the held-key set for repeat suppression).
  const targetsRef = useRef<Map<number, number>>(new Map());

  useNativeEvent("key", (...args) => {
    const key = args[0] as number;
    const press = args[1] as boolean;
    const button = keyToButton.get(key);
    if (button === undefined) return; // not a game key (e.g. Esc → App's menu handler)

    const targets = targetsRef.current;
    if (press) {
      if (!active) return; // no new presses while a menu is up / project empty
      if (targets.has(key)) return; // OS auto-repeat
      if (focusedId === 0) return; // no focused instance to receive it
      targets.set(key, focusedId);
      backend.pressButton(focusedId, button, true);
    } else {
      const target = targets.get(key);
      if (target === undefined) return; // release with no recorded press
      targets.delete(key);
      backend.pressButton(target, button, false);
    }
  });
}
