// Render an mGB C-major chord to a WAV for the reaper-MCP audio-quality analysis workflow (the
// greenfield counterpart of legacy `pnpm smoke` → /tmp/cli-smoke.wav). Boots the embedded mGB synth,
// stages a three-voice chord (mGB reads one MIDI channel per voice), and dumps the render. Staged by
// `pnpm reaper:analyze-smoke-greenfield` for spectral / loudness inspection.
//
//   retroplug-greenfield-cli build/greenfield-cli/analyze-mgb.js [out.wav]
import { runSession, hostArgs } from "../session";
import { encodeWav } from "../wav";

// One note per channel — mGB's per-voice MIDI inputs form a C-major chord.
const CHORD: number[][] = [
  [0x90, 60, 100], // ch1 C4
  [0x91, 64, 100], // ch2 E4
  [0x92, 67, 100], // ch3 G4
];

runSession((s) => {
  const out = hostArgs()[0] || "/tmp/cli-smoke-greenfield.wav";

  const id = s.project.systems.loadMgb();
  if (id == null) throw new Error("loadMgb failed");
  s.audio.renderAudio(1500); // boot past the mGB splash (the store's midi-routing role is projected)

  CHORD.forEach((m) => s.audio.stageMidiIn(m));
  const pcm = s.audio.renderAudio(2000); // the chord rings
  if (!s.backend.writeFile(out, encodeWav(pcm))) throw new Error(`write failed: ${out}`);
  console.log(`greenfield-cli: mGB C-major chord → ${out}`);
});
