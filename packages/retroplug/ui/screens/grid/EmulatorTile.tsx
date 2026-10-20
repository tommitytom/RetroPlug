// One emulator tile: the running Game Boy screen for a system, blitted into an LVGL Canvas.
//
// On every "frame" render tick it pulls the system's framebuffer (backend.getFrame — synchronous,
// so no async drop-frame guard) and pushes it into the Canvas via setBuffer. Clicking the
// tile focuses its system (LVGL bubbles the child click up to the root View's onClick). An unfocused
// tile is dimmed by a translucent black overlay — the lvgl-js way, since opacity on a Canvas is dropped
// (only Image widgets route it) and opacity units are 0..1 floats (0.5 = LV_OPA_50).

import { useCallback, useRef } from "react";
import { EAlignType, Text } from "lvgljs-ui";

import { useStores } from "../../stores/useStores";
import { useNativeEvent } from "../../lvgl/useNativeEvent";
import { useFrameSampled } from "../../lvgl/useFramePoll";
import { Box } from "../../lvgl/Box";
import { PixelCanvas } from "../../lvgl/PixelCanvas";
import type { CanvasHandle } from "../../lvgl/lv";
import { tagTestId } from "../../lvgl/StableSlot";
import { LsdjOverlay } from "./LsdjOverlay";
import { RisaOverlay } from "./RisaOverlay";
import { getRenderJobs, cancelRender, dismissRenderJob, pickActiveRenderJob, renderBadgeLabel, type RenderJobStatus } from "../../lvgl/render";

export function EmulatorTile({
  systemId,
  focused,
  single,
  width,
  height,
  onFocus,
  dimTestId,
}: {
  systemId: number;
  focused: boolean;
  /** The only system in the project. A lone tile fills the window with no focus border and no dim — the
   *  focus cue only means something when there's another tile to distinguish it from. */
  single?: boolean;
  width: number;
  height: number;
  onFocus?: () => void;
  /** Test tag for the dim overlay — present only while the tile is unfocused (a live focus signal). */
  dimTestId?: string;
}) {
  const { backend } = useStores();
  const canvasRef = useRef<CanvasHandle | null>(null);

  // The active background render badge for this system (or null): poll this system's jobs, auto-dismiss the
  // finished ones, and surface the active render (or a lingering error). Re-renders only when the LABEL
  // changes (so once a second while rendering). getRenderJobs() is inert (empty) in the headless harness.
  const sampleRenderJob = useCallback((): RenderJobStatus | null => {
    const jobs = getRenderJobs();
    for (const j of jobs)
      if (j.systemId === systemId && (j.state === "done" || j.state === "cancelled"))
        dismissRenderJob(j.id); // the render finished (file written) — drop the job, vanish the badge
    return pickActiveRenderJob(jobs, systemId);
  }, [systemId]);
  const renderJob = useFrameSampled(sampleRenderJob, renderJobKey);

  useNativeEvent("frame", () => {
    const frame = backend.getFrame(systemId);
    if (!frame || !frame.published) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    // .slice().buffer: the pixels may be a subarray view of the RPC wire buffer; copy to a fresh
    // ArrayBuffer of exactly the frame bytes (setBuffer wants a whole-buffer object).
    canvas.setBuffer(frame.pixels.slice().buffer, frame.width, frame.height);
  });

  // The focus cue only distinguishes one tile from another, so a lone tile shows neither: no accent
  // border and no dim.
  const showBorder = focused && !single;
  const dim = !focused && !single;

  return (
    <Box onClick={onFocus} style={{ width, height, "background-color": "#000000" }}>
      <PixelCanvas canvasRef={canvasRef} width={width} height={height} />
      {dim && (
        <Box
          innerRef={dimTestId ? tagTestId(dimTestId) : undefined}
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-color": "#000000", "background-opacity": 0.5 }}
        />
      )}
      {/* Live LSDj runtime readout (screen + cursor / play / song row / tempo / version / per-channel
          phrase). Renders nothing for a non-LSDj ROM, so it's mounted unconditionally; its per-frame
          re-renders stay in this child subtree. */}
      <LsdjOverlay systemId={systemId} width={width} testId="lsdj-overlay" />
      {/* The risa twin — a live NES-tracker readout (screen + cursor / mode / tempo / version / per-track
          phrase). Renders nothing for a non-risa ROM, so it too is mounted unconditionally. */}
      <RisaOverlay systemId={systemId} width={width} testId="risa-overlay" />
      {showBorder && (
        // Accent border on the focused tile — the visible focus cue (the dim is imperceptible on a
        // near-black GB screen). Drawn as a transparent full-tile overlay layered OVER the Canvas, not
        // a border-width on the container: an LVGL border insets the content box, which would shift the
        // framebuffer child in by 2px. As an overlay the 2px stroke lands on the tile's outer ring,
        // painted over the Canvas edge (a couple of rim pixels), so the picture never moves.
        <Box
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-opacity": 0, "border-width": 2, "border-color": "#4a86e8" }}
        />
      )}
      {renderJob && (
        // Background-render badge along the tile's bottom: how much audio has been rendered so far. Tap while
        // rendering to cancel; tap an error to dismiss. (The click also bubbles to focus the tile — harmless.)
        <Box
          onClick={() => (renderJob.state === "rendering" ? cancelRender(renderJob.id) : dismissRenderJob(renderJob.id))}
          align={{ type: EAlignType.ALIGN_BOTTOM_MID, pos: [0, 0] }}
          style={{
            width,
            height: renderBarH(height),
            "background-color": renderJob.state === "rendering" ? "#101010" : "#7f1d1d",
            "background-opacity": 0.75,
          }}
        >
          <Text style={{ "text-color": "#ffffff", "font-size": renderFont(height), width, height: renderBarH(height) }}>
            {renderBadgeLabel(renderJob)}
          </Text>
        </Box>
      )}
    </Box>
  );
}

/** The badge's dedupe signature: "" for no badge, so an idle tile never re-renders for it. */
function renderJobKey(job: RenderJobStatus | null): string {
  return job ? `${job.id}:${renderBadgeLabel(job)}` : "";
}

/** The render badge bar height / font, scaled to the tile (kept readable on a small tile). */
function renderBarH(height: number): number {
  return Math.max(10, Math.round(height * 0.16));
}
function renderFont(height: number): number {
  return Math.max(8, Math.round(renderBarH(height) * 0.62));
}
