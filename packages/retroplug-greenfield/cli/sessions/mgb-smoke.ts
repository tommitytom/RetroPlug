// The greenfield CLI's example session — a self-contained smoke test of the whole pipeline: boot a
// session, load the embedded mGB synth (no external ROM), render a bit of audio, and dump a screenshot.
// Proves the standalone binary runs a TS-authored session against the real greenfield backend + cores.
//
//   retroplug-greenfield-cli build/greenfield-cli/mgb-smoke.js
import { runSession } from "../session";

runSession((s) => {
  const id = s.project.systems.loadMgb();
  if (id == null) throw new Error("loadMgb failed");
  s.audio.renderAudio(1500); // boot + render past the mGB splash
  s.audio.screenshot(id, "/tmp/greenfield-cli-mgb.png");
  console.log("greenfield-cli: rendered mGB → /tmp/greenfield-cli-mgb.png");
});
