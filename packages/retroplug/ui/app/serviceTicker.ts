// One frame's worth of the contributed UI services (docs/modular-graph-phase-f4b-design.md section 4.5): each service
// starts once for a mount, and every frame runs the polls whose own counter has come round, one answer for whether
// anything moved. Kept out of the hook so the cadence, the repaint answer and each service's isolation are testable
// with no display.
import type { UiHost, UiService } from "@retroplug/ui/contribution";

/** Report one service's failure and carry on: a device whose facet is gone must not stop the others from starting or
 *  from ticking, which is what the separate hooks this replaced gave (native's event emitter isolated one listener's
 *  throw from another's). */
function reportFault(service: UiService, where: string, e: unknown): void {
  console.warn(`[services] ${service.id}: its ${where} threw: ${e instanceof Error ? e.message : String(e)}`);
}

/** Run each service's `start` for one mount, once. `started` is that mount's own record of what has run, which the
 *  hook keeps in a ref across re-renders: a start emits (a repaint), so a start keyed on a host object that a render
 *  rebuilt would run again on every render and loop. */
export function startServices(services: readonly UiService[], host: UiHost, started: Set<string>): void {
  for (const service of services) {
    if (started.has(service.id)) continue;
    started.add(service.id);
    try {
      service.start?.(host);
    } catch (e) {
      reportFault(service, "start", e);
    }
  }
}

/** Advance the counters one frame: run every service whose counter reaches its own `frames`, and answer whether any of
 *  them reported movement (the caller repaints once for the frame, not once per service). `ticks` is the per-service
 *  counter, which the caller keeps across frames; a list that changed length is re-sized here. */
export function tickServices(services: readonly UiService[], host: UiHost, ticks: number[]): boolean {
  if (ticks.length !== services.length) ticks.splice(0, ticks.length, ...services.map(() => 0));
  let moved = false;
  for (let i = 0; i < services.length; i++) {
    const poll = services[i].poll;
    if (!poll) continue;
    if (++ticks[i] < poll.frames) continue;
    ticks[i] = 0;
    try {
      if (poll.run(host)) moved = true;
    } catch (e) {
      reportFault(services[i], "poll", e);
    }
  }
  return moved;
}
