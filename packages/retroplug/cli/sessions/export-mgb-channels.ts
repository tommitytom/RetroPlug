// Render an mGB C-major chord and export its FOUR Game Boy sound channels (Pulse 1, Pulse 2, Wave,
// Noise) via the per-channel pull path (renderAudioPerChannel → one interleaved-stereo buffer per
// channel). Writes all three export shapes so the whole GB channel-split surface is exercised headlessly:
//   (a) one 8-channel multichannel WAV       <prefix>_multi.wav
//   (b) four stereo WAVs (per channel)        <prefix>_pulse1.wav … _noise.wav
//   (c) eight mono WAVs (per channel L/R)     <prefix>_pulse1-l.wav … _noise-r.wav
// GB-as-4-stereo-buffers is the source of truth; (a)/(c) derive by pure TS interleave/deinterleave.
//
//   retroplug-greenfield-cli build/greenfield-cli/export-mgb-channels.js [outPrefix]
import { runSession, hostArgs } from "../session";
import { encodeWav, interleaveStereoStreams, deinterleaveStereo } from "../wav";

// One note per channel — mGB's per-voice MIDI inputs form a C-major chord (matches analyze-mgb).
const CHORD: number[][] = [
  [0x90, 60, 100], // ch1 C4
  [0x91, 64, 100], // ch2 E4
  [0x92, 67, 100], // ch3 G4
];

// GB_channel_t order, matching SameBoySystem::channelLayout().
const CHANNEL_NAMES = ["pulse1", "pulse2", "wave", "noise"];

runSession((s) => {
  const prefix = hostArgs()[0] || "/tmp/mgb-channels-greenfield";

  const id = s.project.systems.loadMgb();
  if (id == null) throw new Error("loadMgb failed");
  s.audio.renderAudio(1500); // boot past the mGB splash (the store's midi-routing role is projected)

  CHORD.forEach((m) => s.audio.stageMidiIn(m));
  const bufs = s.audio.renderAudioPerChannel(id, 2000); // 4 interleaved-stereo buffers, one per channel
  if (bufs.length === 0) throw new Error("renderAudioPerChannel returned no streams");
  const sr = s.audio.sampleRate();

  const write = (name: string, bytes: Uint8Array) => {
    if (!s.backend.writeFile(name, bytes)) throw new Error(`write failed: ${name}`);
    console.log(`greenfield-cli: ${name}`);
  };

  // (a) one multichannel WAV — the 4 stereo streams interleaved into 8 channels.
  write(`${prefix}_multi.wav`, encodeWav(interleaveStereoStreams(bufs), sr, bufs.length * 2));

  // (b) individual stereo WAVs — each channel stream is already stereo.
  bufs.forEach((buf, i) => write(`${prefix}_${CHANNEL_NAMES[i] ?? `ch${i}`}.wav`, encodeWav(buf, sr, 2)));

  // (c) individual mono WAVs — each stereo stream deinterleaved into L and R.
  bufs.forEach((buf, i) => {
    const [l, r] = deinterleaveStereo(buf);
    const base = CHANNEL_NAMES[i] ?? `ch${i}`;
    write(`${prefix}_${base}-l.wav`, encodeWav(l, sr, 1));
    write(`${prefix}_${base}-r.wav`, encodeWav(r, sr, 1));
  });

  console.log(`greenfield-cli: mGB 4-channel export (@${sr} Hz) → ${prefix}_*`);
});
