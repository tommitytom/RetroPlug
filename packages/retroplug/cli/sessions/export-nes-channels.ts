// Render an NES ROM and export its two 2A03 "stereo-mod" output pins — Pulse (Square1+Square2) and TND
// (DMC+Triangle+Noise) — plus the lumped Expansion term, as separate MONO streams via the per-channel
// pull path (renderAudioPerChannel). spec/10 §5. Unlike the Game Boy (4 stereo streams), the NES pins
// are mono, so each renderAudioPerChannel buffer is interleaved-stereo with a silent right lane; we keep
// the left lane. Writes:
//   (a) three mono WAVs (one per pin)        <prefix>_pulse.wav / _tnd.wav / _expansion.wav
//   (b) one 3-channel WAV (the pins combined) <prefix>_pins.wav
//
//   retroplug-cli build/cli/export-nes-channels.js <rom.nes> [outPrefix]
import { runSession, hostArgs } from "../session";
import { encodeWav, deinterleaveStereo } from "../wav";
import { syncDspFromStore } from "../../src/appHost";

// ch1 NoteOn C4 → n8-midi drives APU Pulse1 (so the Pulse pin rings while TND/Expansion stay quiet).
const NOTE_ON_CH1 = [0x90, 60, 100];

// channelLayout() order for StereoModPins (MesenNesSystem::channelLayout).
const PIN_NAMES = ["pulse", "tnd", "expansion"];

runSession((s) => {
  const romPath = hostArgs()[0];
  const prefix = hostArgs()[1] || "/tmp/nes-channels";
  if (!romPath) throw new Error("usage: export-nes-channels <rom.nes> [outPrefix]");

  // Adopt the NES with the "mesen" role in StereoModPins mode — capture engages at construct/onActivate,
  // so it MUST be set here, not live. The host-MIDI role lets a note reach the APU. adopt is quiet, so
  // project the store into the DSP runtime by hand (bootSession's onSystemsChange hook doesn't fire).
  s.project.systems.adopt({
    romPath,
    roles: [
      { kind: "nes-n8-midi", config: {} },
      { kind: "mesen", config: { channelExportMode: 1 } },
    ],
  });
  syncDspFromStore(s.project, s.dsp);

  const id = s.project.systems.view()[0]?.id;
  if (id == null) throw new Error("adopt failed (no system)");

  s.audio.renderAudio(1000);          // boot + init settle
  s.audio.stageMidiIn(NOTE_ON_CH1);   // prime — n8-midi drops the first MIDI message
  s.audio.stageMidiIn(NOTE_ON_CH1);

  const bufs = s.audio.renderAudioPerChannel(id, 2000); // 3 interleaved-stereo buffers (mono: R silent)
  if (bufs.length !== 3) throw new Error(`expected 3 NES pin streams, got ${bufs.length}`);
  const sr = s.audio.sampleRate();

  const write = (name: string, bytes: Uint8Array) => {
    if (!s.backend.writeFile(name, bytes)) throw new Error(`write failed: ${name}`);
    console.log(`cli: ${name}`);
  };

  // (a) one mono WAV per pin — the left lane carries the signal (the pins are mono).
  const mono = bufs.map((b) => deinterleaveStereo(b)[0]);
  mono.forEach((l, i) => write(`${prefix}_${PIN_NAMES[i] ?? `pin${i}`}.wav`, encodeWav(l, sr, 1)));

  // (b) one combined 3-channel WAV (Pulse | TND | Expansion interleaved).
  const frames = mono[0].length;
  const pins = new Float32Array(frames * 3);
  for (let i = 0; i < frames; i++) {
    pins[i * 3 + 0] = mono[0][i];
    pins[i * 3 + 1] = mono[1][i];
    pins[i * 3 + 2] = mono[2][i];
  }
  write(`${prefix}_pins.wav`, encodeWav(pins, sr, 3));

  console.log(`cli: NES stereo-mod pins (@${sr} Hz) → ${prefix}_*`);
});
