// Run the product's UI services (docs/modular-graph-phase-f4b-design.md section 4.5): the device state the menu shows
// that no store can see, kept current without a hook of its own per device.
//
// A service is data a package contributes (`UiService`): `start` runs at every MOUNT (a device reclaimed from
// config.json, its latches put back), `subscribe` asks for a repaint when the native host pushes a change, and `poll`
// runs every `frames` render frames and answers whether what it shows MOVED. So each device's cadence and its version
// latch live in the package, and this hook is the one place a frame becomes a poll and a `true` becomes a repaint.
import { useEffect, useRef } from "react";

import type { UiHost } from "@retroplug/ui/contribution";
import { uiServices } from "../host/menuContributions";
import { useForceUpdate, useNativeEvent } from "@retroplug/ui-react";
import { startServices, tickServices } from "./serviceTicker";

export function useUiServices(host: UiHost): void {
  // The registry's list: the same array until a commit adds services, so the effects below re-run only then. The
  // caller (App) re-renders on the counter, which is what brings a new list here.
  const services = uiServices();
  const rerender = useForceUpdate();
  // Every subscription through one effect over the list: a service a pack adds is subscribed when its list arrives, and
  // the old subscriptions are dropped with the old list.
  useEffect(() => {
    const offs = services.flatMap((service) => (service.subscribe ? [service.subscribe(rerender)] : []));
    return () => offs.forEach((off) => off());
  }, [services, rerender]);
  // The host the mount's services started with. Held in a ref so a fresh host object (App rebuilds it when its own
  // inputs move) cannot restart them: `start` emits, which re-renders, so a start that followed the host's identity
  // would loop.
  const hostRef = useRef(host);
  hostRef.current = host;
  // The mount's own record of what has started, which `startServices` reads: one `start` per service per mount, so a
  // longer list starts only the services it added.
  const started = useRef(new Set<string>());
  useEffect(() => {
    startServices(services, hostRef.current, started.current);
  }, [services]);
  // One frame event, a counter per service, so two devices keep their own cadence rather than the fastest one's;
  // `tickServices` re-sizes the counters when the list's length changes.
  const ticks = useRef<number[]>(services.map(() => 0));
  useNativeEvent("frame", () => {
    if (tickServices(services, host, ticks.current)) rerender();
  });
}
