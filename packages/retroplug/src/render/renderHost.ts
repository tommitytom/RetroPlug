// The render surface's host (docs/modular-graph-phase-f4b-design.md section 3.2): what a cart's render hooks drive of
// the booted system, and the file I/O a hook may read with. Built per render over the render's own context - the
// backend and the audio driver it already holds. The render's OUTPUT stream is not here: it is the request's own sink,
// built after this host and passed to the one hook that captures into it.
import type { Host } from "@retroplug/core/host";
import type { RenderContext } from "./types";
import { filesHost } from "../hostGroups";

export type RenderHostGroups = Host<"emulator" | "files">;

export function renderHost(ctx: RenderContext): RenderHostGroups {
  return {
    emulator: {
      renderAudio: (ms) => ctx.audio.renderAudio(ms),
      readCpu: (id, addr) => ctx.backend.readCpu(id, addr),
      readRam: (id) => ctx.backend.readRam(id),
      writeRam: (id, offset, bytes) => ctx.backend.writeRam(id, offset, bytes),
      pressButton: (id, button, down) => ctx.audio.pressButton(id, button, down),
    },
    files: filesHost(ctx.backend),
  };
}
