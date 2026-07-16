// The CLI's example session — a self-contained smoke test of the whole pipeline: boot a
// session, load the embedded mGB synth (no external ROM), render a bit of audio, and dump a screenshot.
// Proves the standalone binary runs a TS-authored session against the real backend + cores.
//
//   retroplug-cli build/cli/mgb-smoke.js
import { runSession } from "../session";

runSession((s) => {
  const id = s.project.systems.loadMgb();
  if (id == null) throw new Error("loadMgb failed");
  s.audio.renderAudio(1500); // boot + render past the mGB splash
  s.audio.screenshot(id, "/tmp/cli-mgb.png");
  console.log("cli: rendered mGB → /tmp/cli-mgb.png");
});
