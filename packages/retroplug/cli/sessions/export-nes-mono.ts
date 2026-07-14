// Render an NES ROM and export its FIVE individual core APU channels — Square1, Square2, Triangle, Noise,
// DMC — as separate MONO streams via the per-channel pull path (renderAudioPerChannel). spec/10 §5b.
// These are raw pre-DAC linear levels labelled "does not sum" (the mix's non-linear DACs over group sums
// mean channels sharing a pin cross-compress) — good for isolation/analysis, not a bit-exact
// decomposition. Each buffer is interleaved-stereo with a silent right lane (mono); we keep the left lane.
//   (a) five mono WAVs (one per channel)     <prefix>_square1.wav … _dmc.wav
//   (b) one 5-channel WAV (all combined)      <prefix>_mono.wav
//
//   retroplug-cli build/cli/export-nes-mono.js <rom.nes> [outPrefix]
import { runSession, hostArgs } from "../session";
import { encodeWav, deinterleaveStereo } from "../wav";
import { syncDspFromStore } from "../../src/appHost";

// One note per core APU channel (n8-midi routing): ch1→Pulse1, ch2→Pulse2, ch3→Triangle, ch4→Noise.
// (DMC/ch5 needs a sample bank, unavailable headless — it stays silent here.)
const NOTES: number[][] = [
  [0x90, 60, 100], // ch1 → Square1
  [0x91, 64, 100], // ch2 → Square2
  [0x92, 67, 100], // ch3 → Triangle
  [0x93, 48, 100], // ch4 → Noise (note in the ROM's 36–67 range)
];

// channelLayout() order for IndividualMono (MesenNesSystem::channelLayout).
const CH_NAMES = ["square1", "square2", "triangle", "noise", "dmc"];

runSession((s) => {
  const romPath = hostArgs()[0];
  const prefix = hostArgs()[1] || "/tmp/nes-mono";
  if (!romPath) throw new Error("usage: export-nes-mono <rom.nes> [outPrefix]");

  // Adopt with the "mesen" role in individualMono mode — capture engages at construct/onActivate,
  // so it MUST be set here, not live. The host-MIDI role lets notes reach the APU. adopt is quiet → project
  // the store into the DSP runtime by hand (bootSession's onSystemsChange hook doesn't fire).
  s.project.systems.adopt({
    romPath,
    roles: [
      { kind: "nes-n8-midi", config: {} },
      { kind: "mesen", config: { channelExportMode: "individualMono" } },
    ],
  });
  syncDspFromStore(s.project, s.dsp);

  const id = s.project.systems.view()[0]?.id;
  if (id == null) throw new Error("adopt failed (no system)");

  s.audio.renderAudio(1000);            // boot + init settle
  // A chord across the core channels (prime — n8-midi drops the first MIDI message).
  NOTES.forEach((m) => s.audio.stageMidiIn(m));
  NOTES.forEach((m) => s.audio.stageMidiIn(m));

  const bufs = s.audio.renderAudioPerChannel(id, 2000); // 5 interleaved-stereo buffers (mono: R silent)
  if (bufs.length !== 5) throw new Error(`expected 5 NES core streams, got ${bufs.length}`);
  const sr = s.audio.sampleRate();

  const write = (name: string, bytes: Uint8Array) => {
    if (!s.backend.writeFile(name, bytes)) throw new Error(`write failed: ${name}`);
    console.log(`cli: ${name}`);
  };

  // (a) one mono WAV per core channel — the left lane carries the signal.
  const mono = bufs.map((b) => deinterleaveStereo(b)[0]);
  mono.forEach((l, i) => write(`${prefix}_${CH_NAMES[i] ?? `ch${i}`}.wav`, encodeWav(l, sr, 1)));

  // (b) one combined 5-channel WAV.
  const frames = mono[0].length;
  const inter = new Float32Array(frames * 5);
  for (let i = 0; i < frames; i++) {
    for (let c = 0; c < 5; c++) inter[i * 5 + c] = mono[c][i];
  }
  write(`${prefix}_mono.wav`, encodeWav(inter, sr, 5));

  console.log(`cli: NES 5 core channels (@${sr} Hz) → ${prefix}_*`);
});
