// The two pieces of held-input bookkeeping the keyboard router (useGameInput), the gamepad router
// (useGamepadInput) and the menu's stick navigation (Menu) each kept by hand.

import { useCallback, useRef } from "react";

import { useStores } from "../stores/useStores";
import { axisToken } from "../../src/keyCodes";

/**
 * Route joypad presses to the focused system, one input "slot" at a time (a key, a pad button, a stick
 * half-axis). The slot → system map does double duty:
 *   - repeat drop: a slot already held ignores further presses (OS key auto-repeat; a stick resting past
 *     its threshold), since the joypad only needs one transition per physical press.
 *   - release-to-original-target: a release goes to the system the press went to, even if focus moved
 *     mid-hold, so a held button never sticks on the wrong (or a gone) instance.
 * Releases are honoured even while inactive, so opening a menu mid-hold can't strand a button down.
 */
export function useHeldRouting(active: boolean, focusedId: number) {
  const { backend } = useStores();
  const targets = useRef(new Map<string, number>());

  const press = (slot: string, button: number): void => {
    if (!active) return; // no new presses while a menu is up / project empty
    if (targets.current.has(slot)) return; // already held
    if (focusedId === 0) return; // no focused instance to receive it
    targets.current.set(slot, focusedId);
    backend.pressButton(focusedId, button, true);
  };

  const release = (slot: string, button: number): void => {
    const target = targets.current.get(slot);
    if (target === undefined) return; // release with no recorded press
    targets.current.delete(slot);
    backend.pressButton(target, button, false);
  };

  return { press, release };
}

/**
 * Edge-detect analog sticks into digital half-axis tokens ("leftx-" / "lefty+", "" = centred), with the
 * axisToken hysteresis. Returns a stable `step(pad, axis, value)` that answers `{ from, to }` when that
 * stick axis changed direction - a flip through centre is one step, from the old token to the new - and
 * null otherwise, so a stick held still produces nothing.
 */
export function useAxisEdges(): (pad: number, axis: string, value: number) => { from: string; to: string } | null {
  const dirs = useRef(new Map<string, string>());
  return useCallback((pad: number, axis: string, value: number) => {
    const key = `${pad}:${axis}`;
    const from = dirs.current.get(key) ?? "";
    const to = axisToken(axis, value, from);
    if (to === from) return null;
    dirs.current.set(key, to);
    return { from, to };
  }, []);
}
