// useGamepadInput — route SDL game-controller buttons into the focused emulator's joypad.
//
// The native GamepadManager (ticked from PluginUI::uiIdle) polls SDL and pushes each transition
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
import { buildGamepadToButton, axisToken } from "../../src/keyCodes";

export function useGamepadInput({ active, focusedId }: { active: boolean; focusedId: number }) {
  const { backend } = useStores();
  const bindings = useBindings();
  const padToButton = useMemo(() => buildGamepadToButton(bindings.gamepad), [bindings.gamepad]);
  // `${pad}:${token}` → the system id its press was routed to (held-slot set + release-to-original-target).
  const targetsRef = useRef<Map<string, number>>(new Map());
  // `${pad}:${axisName}` → the half-axis token currently held for that stick axis ("" = centered).
  const axisDirRef = useRef<Map<string, string>>(new Map());

  // Press/release a GB button for one input "slot" (a pad button or a half-axis direction), routed to the
  // focused system on press and back to the recorded target on release — shared by the button + axis paths.
  const pressSlot = (slot: string, button: number) => {
    const targets = targetsRef.current;
    if (!active) return; // no new presses while a menu is up / project empty
    if (targets.has(slot)) return; // already held
    if (focusedId === 0) return; // no focused instance to receive it
    targets.set(slot, focusedId);
    backend.pressButton(focusedId, button, true);
  };
  const releaseSlot = (slot: string, button: number) => {
    const targets = targetsRef.current;
    const target = targets.get(slot);
    if (target === undefined) return; // release with no recorded press
    targets.delete(slot);
    backend.pressButton(target, button, false);
  };

  useNativeEvent("gamepad-button", (...args) => {
    const pad = args[0] as number;
    const name = args[1] as string;
    const press = args[2] as boolean;
    const button = padToButton.get(name);
    if (button === undefined) return; // unbound controller button
    const slot = `${pad}:${name}`;
    if (press) pressSlot(slot, button);
    else releaseSlot(slot, button);
  });

  // Analog stick → d-pad: convert the continuous axis to a digital half-axis token (with hysteresis) and
  // press/release the bound button as the direction changes. A flip through centre releases the old
  // direction then presses the new one; centering releases.
  useNativeEvent("gamepad-axis", (...args) => {
    const pad = args[0] as number;
    const axisName = args[1] as string;
    const value = args[2] as number;
    const axisKey = `${pad}:${axisName}`;
    const cur = axisDirRef.current.get(axisKey) ?? "";
    const next = axisToken(axisName, value, cur);
    if (next === cur) return;
    axisDirRef.current.set(axisKey, next);
    if (cur) {
      const b = padToButton.get(cur);
      if (b !== undefined) releaseSlot(`${pad}:${cur}`, b); // release the direction we were holding
    }
    if (next) {
      const b = padToButton.get(next);
      if (b !== undefined) pressSlot(`${pad}:${next}`, b);
    }
  });
}
