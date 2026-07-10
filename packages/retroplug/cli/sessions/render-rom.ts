// Render a ROM to a WAV — the greenfield CLI's parameterized example. Reads the ROM + output paths (and
// an optional duration) from the command line, loads the ROM, renders that many ms of audio, and writes
// a 16-bit WAV. Demonstrates both argv forwarding (hostArgs) and audio output (encodeWav) on the
// standalone binary, headless, with no Node.
//
//   retroplug-greenfield-cli build/greenfield-cli/render-rom.js <rom> <out.wav> [ms]
import { runSession, hostArgs } from "../session";
import { encodeWav } from "../wav";

runSession((s) => {
  const [romPath, outPath, msArg] = hostArgs();
  if (!romPath || !outPath) throw new Error("usage: render-rom.js <rom> <out.wav> [ms]");

  // addSystem loads exactly this ROM (no sibling-.rplg deferral, no stray disk writes) and — because
  // bootSession installed the onSystemsChange hook — projects it into the DSP runtime so audio renders.
  const id = s.project.systems.addSystem(romPath);
  if (id == null) throw new Error(`could not load ROM: ${romPath}`);

  const ms = Number(msArg) || 2000;
  const pcm = s.audio.renderAudio(ms); // interleaved L/R float32 @ 44100
  if (!s.backend.writeFile(outPath, encodeWav(pcm))) throw new Error(`write failed: ${outPath}`);
  console.log(`greenfield-cli: rendered ${romPath} → ${outPath} (${ms}ms)`);
});
