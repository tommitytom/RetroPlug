// The `render` CLI tool — render a ROM (+ its battery .sav / a savestate) straight to WAV from the command
// line, no script authoring, no Node. Exported as `renderTool` (a CliTool) and registered in cli/tools.ts;
// the dispatcher (cli/cli.ts, the bundle compiled into retroplug-cli) runs it under a booted session, so an
// end user with just the executable can:
//
//   retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--duration t] [--sample-rate hz]
//                              [--split mix|channels|pins] [--bpm n] [--transport] [--no-start]
//
// This is a thin CLI front: parseRenderArgs turns argv into a RenderOpts, then the shared render library
// (src/render) does the work — the same code the UI/background render worker runs, so improvements land in
// both. --list-songs is handled here (it's a query, not a render). See RENDER_HELP in renderArgs.ts for the
// full flag reference and src/render/render.ts for the render semantics (LSDj auto-length, song selection).
import { parseRenderArgs, RENDER_SUMMARY, RENDER_HELP, type RenderOpts } from "../renderArgs";
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { platformOf, readSav, readRisaSongs, runRenderJob } from "../../src/render";

/** --list-songs: print the sav's populated song slots and exit (renders nothing). GB (LSDj) + NES (risa). */
function listSongs(s: Session, o: RenderOpts): void {
  const platform = platformOf(o.rom);
  if (platform === "gb") {
    const { path, sav } = readSav(s, o);
    console.log(`songs in ${path}:`);
    sav.projects.forEach((p, i) => { if (p) console.log(`  ${i}: ${p.name || "(unnamed)"}`); });
    if (sav.projects.every((p) => !p)) console.log("  (no named projects — only the working song)");
    return;
  }
  if (platform === "nes") {
    const { path, songs } = readRisaSongs(s, o);
    console.log(`songs in ${path}:`);
    if (!songs.length) console.log("  (no saved songs in the catalog)");
    for (const song of songs) console.log(`  ${song.index}: ${song.name || "(unnamed)"}`);
    return;
  }
  throw new Error(`render: --list-songs is a Game Boy (LSDj) / NES (risa) feature (got ${platform})`);
}

function runRender(s: Session, args: string[]): void {
  const o = parseRenderArgs(args);
  if (o.listSongs) { listSongs(s, o); return; }
  // Default hooks: logging → console, no progress/cancel. The Session structurally satisfies RenderContext.
  runRenderJob(s, o);
}

/** The `render` CLI tool: name + summary for the top-level index, detailed --help, and the render body.
 *  Registered in cli/tools.ts; the dispatcher (cli/cli.ts) runs it under a booted session. */
export const renderTool: CliTool = {
  name: "render",
  summary: RENDER_SUMMARY,
  help: RENDER_HELP,
  run: runRender,
};
