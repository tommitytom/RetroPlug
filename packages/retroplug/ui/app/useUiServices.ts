// Run the product's UI services (docs/modular-graph-phase-f4b-design.md section 4.5): the device state the menu shows
// that no store can see, kept current without a hook of its own per device.
//
// A service is data a package contributes (`UiService`): `start` runs once when the UI is up (a device reclaimed from
// config.json), `subscribe` asks for a repaint when the native host pushes a change, and `poll` runs every `frames`
// render frames and answers whether what it shows MOVED. So each device's cadence and its version latch live in the
// package, and this hook is the one place a frame becomes a poll and a `true` becomes a repaint.
import { useEffect, useRef } from "react";

import type { UiHost } from "@retroplug/ui/contribution";
import { uiServices } from "../host/menuContributions";
import { useRerenderOn } from "../hooks/useRerender";
import { useNativeEvent } from "../lvgl/useNativeEvent";
import { tickServices } from "./serviceTicker";

export function useUiServices(host: UiHost): void {
  const services = uiServices(); // a module constant, so the dependencies below are stable
  // Every subscription through the shell's one re-render hook, whose own force-update the polls below share: the list
  // is module data, so the call is stable and this runs once per mount.
  const rerender = useRerenderOn(...services.flatMap((service) => (service.subscribe ? [service.subscribe] : [])));
  // One `start` per mount (the host is the memoised one App built): a device reclaimed from config.json, once.
  useEffect(() => {
    for (const service of services) service.start?.(host);
  }, [services, host]);
  // One frame event, a counter per service, so two devices keep their own cadence rather than the fastest one's.
  const ticks = useRef<number[]>(services.map(() => 0));
  useNativeEvent("frame", () => {
    if (tickServices(services, host, ticks.current)) rerender();
  });
}
