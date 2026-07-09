// useGamepadInput — route SDL game-controller buttons into the focused emulator's joypad.
//
// The native GamepadManager (ticked from PluginGreenfieldUI::uiIdle) polls SDL and pushes each transition
// onto the "gamepad-button" bus as [pad, name, press]: pad = SDL_JoystickID (stable across hot-plug),
// name = SDL canonical button name ("dpright"/"a"/"start"). This hook resolves the name to a Game Boy
// button via the active GAMEPAD bindings and fires backend.pressButton at the focused system — the exact
// twin of useGameInput (keyboard), sharing its capture-on-press / release-to-original-target discipline.
//
// The targets map is keyed by `${pad}:${name}` (not just the button value) so multiple pads — and multiple
// buttons that resolve to the same GB button — each hold and release independently. Releases are always
// processed (even when inactive) so opening a menu mid-hold can't strand a button down on a core.

import { useMemo, useRef } from "react";

import { useStores, useBindings } from "../stores/useStores";
import { useNativeEvent } from "../lvgl/useNativeEvent";
import { buildGamepadToButton } from "../../src/keyCodes";

export function useGamepadInput({ active, focusedId }: { active: boolean; focusedId: number }) {
  const { backend } = useStores();
  const bindings = useBindings();
  const padToButton = useMemo(() => buildGamepadToButton(bindings.gamepad), [bindings.gamepad]);
  // `${pad}:${name}` → the system id its press was routed to (also the held-button set for repeat drop).
  const targetsRef = useRef<Map<string, number>>(new Map());

  useNativeEvent("gamepad-button", (...args) => {
    const pad = args[0] as number;
    const name = args[1] as string;
    const press = args[2] as boolean;
    const button = padToButton.get(name);
    if (button === undefined) return; // unbound controller button

    const slot = `${pad}:${name}`;
    const targets = targetsRef.current;
    if (press) {
      if (!active) return; // no new presses while a menu is up / project empty
      if (targets.has(slot)) return; // already held (SDL diffs, so defensive)
      if (focusedId === 0) return; // no focused instance to receive it
      targets.set(slot, focusedId);
      backend.pressButton(focusedId, button, true);
    } else {
      const target = targets.get(slot);
      if (target === undefined) return; // release with no recorded press
      targets.delete(slot);
      backend.pressButton(target, button, false);
    }
  });
}
