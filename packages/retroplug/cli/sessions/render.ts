// The `retroplug-cli render` subcommand — render a ROM (+ its battery .sav / a savestate) straight to
// WAV from the command line, no script authoring, no Node. This session is COMPILED INTO the
// retroplug-cli binary (tjsc bytecode, see packages/native/CMakeLists.txt) and reached via the `render`
// subcommand, so an end user with just the executable can:
//
//   retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--ms n]
//                              [--split mix|channels|pins|mono] [--bpm n] [--transport] [--no-start]
//
// A missing --sav auto-pairs the sibling <rom>.sav (native resolveSavPath). By default it presses Start
// on boot so a saved song (e.g. LSDj) actually plays — pass --no-start to render raw boot audio. The
// --split modes fold in the per-channel/stem exports (GB 4 channels; NES 3 pins / 5 mono core stems).
import { runSession, hostArgs } from "../session";
import { encodeWav, interleaveStereoStreams, deinterleaveStereo } from "../wav";
import { parseRenderArgs, type RenderOpts, type SplitMode } from "../renderArgs";
import { syncDspFromStore } from "../../src/appHost";
import { extensionLower, replaceExtension } from "../../src/pathUtil";
import type { Session } from "../session";

const GB_START = 7; // GameboyButton::Start — LSDj/mGB begin playback on a Start press.

// Per-mode stream labels, matching each system's channelLayout() order.
const GB_CHANNELS = ["pulse1", "pulse2", "wave", "noise"]; // SameBoySystem::channelLayout (stereo streams)
const NES_PINS = ["pulse", "tnd", "expansion"]; // MesenNesSystem StereoModPins (mono streams)
const NES_MONO = ["square1", "square2", "triangle", "noise", "dmc"]; // MesenNesSystem IndividualMono (mono)

type Platform = "gb" | "nes" | "gba" | "other";

function platformOf(rom: string): Platform {
  switch (extensionLower(rom)) {
    case ".gb":
    case ".gbc": return "gb";
    case ".nes": return "nes";
    case ".gba": return "gba";
    default: return "other";
  }
}

/** NES channelExportMode for a split mode (pins=1, mono=3); "channels" aliases to pins on NES. */
function nesExportMode(split: SplitMode): number {
  return split === "mono" ? 3 : 1;
}

/** Build the single system, arming construct-time capture when a NES split mode needs it, and project
 *  the store into the DSP runtime. Returns the system id + its platform. */
function buildSystem(s: Session, o: RenderOpts, platform: Platform): number {
  let id: number | null;
  if (platform === "nes" && o.split !== "mix") {
    // NES per-channel capture engages at construct/onActivate, so channelExportMode MUST be set here.
    // adopt is quiet → project the store by hand (bootSession's onSystemsChange hook doesn't fire).
    s.project.systems.adopt({
      romPath: o.rom,
      savPath: o.sav,
      roles: [{ kind: "mesen", config: { channelExportMode: nesExportMode(o.split) } }],
    });
    syncDspFromStore(s.project, s.dsp);
    id = s.project.systems.view()[0]?.id ?? null;
  } else {
    // mix (any platform) + GB channels: addSystem auto-detects roles and — via bootSession's
    // onSystemsChange hook — projects into the DSP runtime. Sibling <rom>.sav auto-pairs when --sav absent.
    id = s.project.systems.addSystem(o.rom, { explicitSav: o.sav });
  }
  if (id == null) throw new Error(`render: could not load ROM: ${o.rom}`);
  if (o.state && s.project.systems.loadState(id, o.state) == null)
    throw new Error(`render: could not load savestate: ${o.state}`);
  return id;
}

/** The WAV output base: --out (as given for mix, or as a prefix for split) else derived from the ROM. */
function outBase(o: RenderOpts): string {
  if (o.split === "mix") return o.out ?? replaceExtension(o.rom, ".wav");
  if (o.out) return o.out.toLowerCase().endsWith(".wav") ? o.out.slice(0, -4) : o.out;
  return replaceExtension(o.rom, "");
}

runSession((s) => {
  const o = parseRenderArgs(hostArgs());
  const platform = platformOf(o.rom);

  // Validate split ↔ platform up front so a bad combo fails before we load anything.
  if (o.split === "pins" || o.split === "mono") {
    if (platform !== "nes") throw new Error(`render: --split ${o.split} is NES-only (got ${platform})`);
  } else if (o.split === "channels" && platform !== "gb" && platform !== "nes") {
    throw new Error(`render: --split channels needs a Game Boy or NES ROM (got ${platform})`);
  }

  const id = buildSystem(s, o, platform);

  s.audio.renderAudio(1500); // settle boot (past the mGB/LSDj splash) before driving playback
  if (o.bpm) s.audio.setBpm(o.bpm);
  if (o.transport) s.audio.setTransport(true);
  if (o.start && platform === "gb") {
    // Press Start so a saved song begins playing (LSDj boots to a menu, silent until Start).
    s.audio.pressButton(id, GB_START, true);
    s.audio.renderAudio(100);
    s.audio.pressButton(id, GB_START, false);
  }

  const sr = s.audio.sampleRate();
  const write = (name: string, bytes: Uint8Array) => {
    if (!s.backend.writeFile(name, bytes)) throw new Error(`render: write failed: ${name}`);
    console.log(`retroplug-cli: ${name}`);
  };
  const base = outBase(o);

  if (o.split === "mix") {
    const pcm = s.audio.renderAudio(o.ms); // interleaved L/R float32
    write(base, encodeWav(pcm, sr, 2));
    console.log(`retroplug-cli: rendered ${o.rom} → ${base} (${o.ms}ms @${sr}Hz)`);
    return;
  }

  const bufs = s.audio.renderAudioPerChannel(id, o.ms);
  if (bufs.length === 0) throw new Error("render: renderAudioPerChannel returned no streams");

  if (platform === "gb") {
    // GB channel streams are STEREO: one stereo WAV per channel + one combined multichannel WAV.
    write(`${base}_multi.wav`, encodeWav(interleaveStereoStreams(bufs), sr, bufs.length * 2));
    bufs.forEach((b, i) => write(`${base}_${GB_CHANNELS[i] ?? `ch${i}`}.wav`, encodeWav(b, sr, 2)));
    console.log(`retroplug-cli: GB ${bufs.length}-channel render (@${sr}Hz) → ${base}_*`);
    return;
  }

  // NES pins/mono streams are MONO (interleaved-stereo, silent R lane): keep the left lane. One mono WAV
  // per stream + one combined N-channel WAV.
  const names = o.split === "mono" ? NES_MONO : NES_PINS;
  const mono = bufs.map((b) => deinterleaveStereo(b)[0]);
  mono.forEach((l, i) => write(`${base}_${names[i] ?? `ch${i}`}.wav`, encodeWav(l, sr, 1)));
  const frames = mono[0].length;
  const combined = new Float32Array(frames * mono.length);
  for (let f = 0; f < frames; f++)
    for (let c = 0; c < mono.length; c++) combined[f * mono.length + c] = mono[c][f];
  write(`${base}_${o.split}.wav`, encodeWav(combined, sr, mono.length));
  console.log(`retroplug-cli: NES ${o.split} (${mono.length} streams @${sr}Hz) → ${base}_*`);
});
