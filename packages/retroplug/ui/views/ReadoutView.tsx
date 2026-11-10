// The readout renderer (docs/modular-graph-phase-f4c-design.md section 5.3): three lines of text and a colour, deduped
// by the view's signature (or by the lines and the flag themselves) so a 60 fps frame tick does not thrash React. It is
// the strip the two debug overlays draw today, with the lines now coming from a package.
import { useCallback, useMemo } from "react";

import type { SystemInfo } from "@retroplug/core/host";
import type { ReadoutFrame, UiHost, ViewDef } from "@retroplug/ui/contribution";
import { DebugReadout, useFrameSampled } from "@retroplug/ui-react";
import { useViewSession } from "./useViewSession";
import { isReadoutSampler, readoutKey, readoutSignature } from "./viewSession";

export function ReadoutView({
  view,
  host,
  system,
  width,
  shown,
}: {
  view: ViewDef;
  host: UiHost;
  system: SystemInfo | null;
  width: number;
  /** The view's own visibility, as the slot above read it (a window view is shown whenever it is open). */
  shown: boolean;
}) {
  const { session: sampler, read } = useViewSession(view, host, system, shown);
  // useFrameSampled wants a memoised sampler: a new identity means a new source, and it resets what shows. The sample
  // reads the session the controller holds NOW, so a frame after a hide or a reopen never samples a closed one.
  const sample = useMemo(
    () =>
      sampler
        ? (): ReadoutFrame | null | undefined =>
            read((open) => (isReadoutSampler(open) ? open.sample() : null))
        : null,
    [sampler, read],
  );
  const signature = useCallback(
    (frame: ReadoutFrame | null): string => {
      if (frame === null) return "";
      return sampler && isReadoutSampler(sampler) ? readoutKey(view, sampler, frame) : readoutSignature(frame);
    },
    [view, sampler],
  );
  const frame = useFrameSampled(sample, signature);
  if (!frame) return null;
  return <DebugReadout lines={[...frame.lines]} playing={frame.playing} width={width} testId={view.id} />;
}
