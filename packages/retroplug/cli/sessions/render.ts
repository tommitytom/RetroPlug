// The `render` CLI tool — render a ROM (+ its battery .sav / a savestate) straight to WAV from the command
// line, no script authoring, no Node. Exported as `renderTool` (a CliTool) and registered in cli/tools.ts;
// the dispatcher (cli/cli.ts, the bundle compiled into retroplug-cli) runs it under a booted session, so an
// end user with just the executable can:
//
//   retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--duration t] [--sample-rate hz]
//                              [--split mix|channels|pins] [--bpm n] [--transport] [--no-start]
//
// This is a thin CLI front: parseRenderArgs turns argv into a RenderOpts, which goes to the engine as a render
// job (backend.engineRun) - the same job the UI's System > Render submits, run by the render library on a worker
// of the engine's offline pool. The tool prints the job's console lines as they arrive and maps its result onto
// the exit code. --list-songs is handled here (it's a query, not a render). See RENDER_HELP in renderArgs.ts for
// the full flag reference and src/render/render.ts for the render semantics (LSDj auto-length, song selection).
import { parseRenderArgs, RENDER_SUMMARY, RENDER_HELP, type RenderOpts } from "../renderArgs";
import type { CliTool } from "../tools";
import { keepAlive, onInterrupt, type Session } from "../session";
import { echoJobLog, setOfflineYield } from "../../src/offline";
import { platformOf, readSav, readRisaSongs, readSmsggdjSongs } from "../../src/render";

declare function setTimeout(fn: () => void, ms: number): unknown; // txiki's timers

/** --list-songs: print the sav's populated song slots and exit (renders nothing). GB (LSDj), NES (risa),
 *  SMS/GG (smsggdj). */
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
  if (platform === "sms" || platform === "gg") {
    const { path, songs } = readSmsggdjSongs(s, o);
    console.log(`songs in ${path}:`);
    if (!songs.length) console.log("  (no saved songs in the catalog)");
    for (const song of songs) console.log(`  ${song.index}: ${song.name || "(unnamed)"}`);
    return;
  }
  throw new Error(
    `render: --list-songs is a Game Boy (LSDj) / NES (risa) / Master System + Game Gear (smsggdj) feature (got ${platform})`,
  );
}

async function runRender(s: Session, args: string[]): Promise<void> {
  const o = parseRenderArgs(args);
  if (o.listSongs) { listSongs(s, o); return; }
  // A render is a minutes-long job in a host with timers: the launcher's bounded pump would give up on it, so run the
  // unbounded pump, and let the job's poll yield to it between waits (src/offline.ts setOfflineYield). The first
  // Ctrl-C cancels the job, which ends cancelled at its next chunk and removes its partial files (the library's
  // discard; an export already at the path stays), so the command fails with "render: cancelled" below rather than
  // reporting success over a cut-short export; a second Ctrl-C is the default action, for a job that does not stop.
  keepAlive();
  setOfflineYield((resume) => void setTimeout(resume, 0));
  try {
    const handle = s.backend.engineRun("render", o, {
      onLog: echoJobLog,
    });
    onInterrupt(() => void handle.cancel());
    const result = await handle;
    if (result.status === "error") throw new Error(result.message);
    if (result.status === "cancelled") throw new Error("render: cancelled");
  } finally {
    setOfflineYield(null);
  }
}

/** The `render` CLI tool: name + summary for the top-level index, detailed --help, and the render body.
 *  Registered in cli/tools.ts; the dispatcher (cli/cli.ts) runs it under a booted session. */
export const renderTool: CliTool = {
  name: "render",
  summary: RENDER_SUMMARY,
  help: RENDER_HELP,
  run: runRender,
};
