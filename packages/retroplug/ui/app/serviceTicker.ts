// One frame's worth of the contributed UI services' polls (docs/modular-graph-phase-f4b-design.md section 4.5): a
// counter per service, so two devices keep their own cadence rather than the fastest one's, and one answer for whether
// anything moved. Kept out of the hook so the cadence and the repaint answer are testable with no display.
import type { UiHost, UiService } from "@retroplug/ui/contribution";

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
    if (poll.run(host)) moved = true;
  }
  return moved;
}
