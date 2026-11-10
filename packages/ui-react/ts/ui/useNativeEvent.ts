// useNativeEvent — subscribe to a native (lvgljs) event channel for a component's lifetime.
//
// The lvgljs `on`/`off` bus is how native pushes events into the UI (`frame`, `key`, `mouse`,
// `gamepad-*`, config/project change notifications, file-browser results). The legacy UI wires it with
// ~7 near-identical inline on/off effects; this collapses them.
//
// The latest `handler` is kept in a ref and invoked through a stable listener, so passing an inline
// closure does NOT re-subscribe every render — the on/off pair runs once per (channel) mount. This is
// the stable-wrapper pattern the legacy Menu uses to avoid churning its bus subscription.

import { useEffect, useRef } from "react";
import { on, off } from "lvgljs";

/** The channels the UI listens to, with their argument lists, read off every emit site: PluginUI.cpp
 *  (plugin editor), sdl/main.cpp (SDL standalone), GamepadPump.hpp (both), and RenderCore.cpp (the UI test
 *  harness, which emits the same shapes). A change on one side has to be mirrored here by hand - nothing
 *  checks the C++. dpf.js also emits `parameter` (LvglJsEngine) and `rpc-message` (JsRpcBridge), which
 *  the UI does not consume through this hook, so they are left out. */
export interface NativeEvents {
  /** One render frame; `tickMs` is the LVGL tick (u32, wraps at ~49.7 days) - the UI's only monotonic clock. */
  frame: [tickMs: number];
  /** DPF key code (unshifted code point), press/release, modifier mask (kModifierShift = bit 0). Every
   *  host sends all three; the standalone once sent two, which a `mod = 0` default hid as "Shift ignored". */
  key: [code: number, press: boolean, mod: number];
  /** DPF button (left=1, right=2, middle=3; the test harness's clickAt sends 0), press/release,
   *  window-space position. Not emitted by SDL. */
  mouse: [button: number, press: boolean, x: number, y: number];
  /** The window's new size. A non-positive size means "re-read Dimensions.window". */
  resize: [width: number, height: number];
  /** OS drag-and-drop: newline-joined absolute paths + window-space drop point. */
  "file-drop": [paths: string, x: number, y: number];
  /** pad = SDL_JoystickID (stable across hot-plug), name = SDL canonical button name ("a", "dpright"). */
  "gamepad-button": [pad: number, name: string, press: boolean];
  /** axis = SDL axis name ("leftx"), value normalised to -1..1. */
  "gamepad-axis": [pad: number, axis: string, value: number];
  "gamepad-connected": [pad: number, name: string];
  "gamepad-disconnected": [pad: number];
}

export function useNativeEvent<C extends keyof NativeEvents>(channel: C, handler: (...args: NativeEvents[C]) => void): void {
  const ref = useRef(handler);
  ref.current = handler;

  useEffect(() => {
    const listener = (...args: unknown[]) => ref.current(...(args as NativeEvents[C]));
    on(channel, listener);
    return () => off(channel, listener);
  }, [channel]);
}
