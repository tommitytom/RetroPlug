// useGamepadInput — route SDL game-controller buttons into the focused emulator's joypad.
//
// The native GamepadManager (ticked from PluginUI::uiIdle) polls SDL and pushes each transition
// onto the "gamepad-button" bus as [pad, name, press]: pad = SDL_JoystickID (stable across hot-plug),
// name = SDL canonical button name ("dpright"/"a"/"start"). This hook resolves the name to a Game Boy
// button via the active GAMEPAD bindings and fires backend.pressButton at the focused system — the exact
// twin of useGameInput (keyboard), sharing its capture-on-press / release-to-original-target discipline.
//
// Hold/release routing is useHeldRouting's; stick edge detection is useAxisEdges'.

import { useMemo } from "react";

import { useBindings } from "../stores/useStores";
import { useNativeEvent } from "../lvgl/useNativeEvent";
import { buildGamepadToButton } from "../../src/keyCodes";
import { useAxisEdges, useHeldRouting } from "./useHeldInput";

export function useGamepadInput({ active, focusedId }: { active: boolean; focusedId: number }) {
  const bindings = useBindings();
  const padToButton = useMemo(() => buildGamepadToButton(bindings.gamepad), [bindings.gamepad]);
  // Slots are `${pad}:${token}`, so multiple pads - and multiple buttons that resolve to the same GB
  // button - each hold and release independently.
  const routing = useHeldRouting(active, focusedId);
  const axisEdge = useAxisEdges();

  useNativeEvent("gamepad-button", (pad, name, press) => {
    const button = padToButton.get(name);
    if (button === undefined) return; // unbound controller button
    if (press) routing.press(`${pad}:${name}`, button);
    else routing.release(`${pad}:${name}`, button);
  });

  // Analog stick → d-pad: the bound button follows the stick's half-axis direction. A flip through centre
  // releases the old direction then presses the new one; centring releases.
  useNativeEvent("gamepad-axis", (pad, axisName, value) => {
    const edge = axisEdge(pad, axisName, value);
    if (!edge) return;
    if (edge.from) {
      const b = padToButton.get(edge.from);
      if (b !== undefined) routing.release(`${pad}:${edge.from}`, b); // release the direction we were holding
    }
    if (edge.to) {
      const b = padToButton.get(edge.to);
      if (b !== undefined) routing.press(`${pad}:${edge.to}`, b);
    }
  });
}
