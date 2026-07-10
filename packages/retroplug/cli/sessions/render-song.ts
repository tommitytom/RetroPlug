// Play a TS-authored sequence and render it to a WAV — the greenfield CLI's event-scripting example.
// Reads the ROM + output paths from argv, builds a short melody as a typed Timeline (no JSON), plays it
// through the render, and writes the result. Demonstrates MIDI/event scripting on the standalone binary.
//
//   retroplug-greenfield-cli build/greenfield-cli/render-song.js <rom> <out.wav> [ms]
import { runSession, hostArgs } from "../session";
import { Timeline, renderTimeline } from "../timeline";
import { encodeWav } from "../wav";

runSession((s) => {
  const [romPath, outPath, msArg] = hostArgs();
  if (!romPath || !outPath) throw new Error("usage: render-song.js <rom> <out.wav> [ms]");

  const id = s.project.systems.addSystem(romPath);
  if (id == null) throw new Error(`could not load ROM: ${romPath}`);

  // A little C-major arpeggio on channel 1 — each note held 350ms, stepping every 400ms.
  const tl = new Timeline()
    .note(200, 60, { durationMs: 350 }) // C4
    .note(600, 64, { durationMs: 350 }) // E4
    .note(1000, 67, { durationMs: 350 }) // G4
    .note(1400, 72, { durationMs: 500 }); // C5

  const ms = Number(msArg) || 2200;
  const pcm = renderTimeline(s, tl, { durationMs: ms, warmupMs: 1000 }); // boot before the sequence
  if (!s.backend.writeFile(outPath, encodeWav(pcm))) throw new Error(`write failed: ${outPath}`);
  console.log(`greenfield-cli: rendered song on ${romPath} → ${outPath} (${ms}ms)`);
});
