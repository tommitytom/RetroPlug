// The `retroplug-cli render` subcommand — render a ROM (+ its battery .sav / a savestate) straight to
// WAV from the command line, no script authoring, no Node. This session is COMPILED INTO the
// retroplug-cli binary (tjsc bytecode, see packages/native/CMakeLists.txt) and reached via the `render`
// subcommand, so an end user with just the executable can:
//
//   retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--ms n] [--sample-rate hz]
//                              [--split mix|channels|pins|mono] [--bpm n] [--transport] [--no-start]
//
// A missing --sav auto-pairs the sibling <rom>.sav (native resolveSavPath). By default it presses Start
// on boot so a saved song (e.g. LSDj) actually plays — pass --no-start to render raw boot audio. The
// --split modes fold in the per-channel/stem exports (GB 4 channels; NES 3 pins / 5 mono core stems).
//
// LSDj length auto-detect: when a valid LSDj sav is loaded (and no --ms is pinned), render to the song's
// HFF stop (the APU master-enable NR52 going off — lsdpack's technique), report the length, and trim the
// WAV to it. --ms forces a fixed duration; --max-ms caps the detection (no-HFF fallback).
//
// LSDj (GB) song selection: a .sav holds up to 32 named projects but LSDj only plays its WORKING song on
// boot, so --song NAME / --song-index N promote a chosen project to the working song before booting
// (decode → assign → re-encode → seed the fresh system). --list-songs prints the sav's song names.
import { runSession, hostArgs } from "../session";
import { encodeWav, deinterleaveStereo } from "../wav";
import { parseRenderArgs, type RenderOpts, type SplitMode } from "../renderArgs";
import { syncDspFromStore } from "../../src/appHost";
import { extensionLower, replaceExtension } from "../../src/pathUtil";
import { siblingSavPath } from "../../src/savPaths";
import { decodeSav, encodeSav, kSavSize } from "../../src/lsdj";
import type { Session } from "../session";

const GB_START = 7; // GameboyButton::Start — LSDj/mGB begin playback on a Start press.

// LSDj song-length auto-detect: LSDj's HFF command stops the song by powering the APU off — a 0 write to
// NR52 ($FF26), the sound master-enable register (bit 7). We poll it each render chunk and stop at the
// high→low edge (the technique lsdpack uses). Hardware-level, so version-independent + tempo/hop-agnostic.
const NR52_ADDR = 0xff26;
const NR52_ON = 0x80; // bit 7 = all-sound-on
const DETECT_CHUNK_MS = 100; // poll granularity (≈ detection precision)
const DETECT_ARM_MS = 2000; // window to confirm playback began (NR52 goes on) after Start
const DETECT_OFF_CHUNKS = 2; // NR52 must read off this many consecutive chunks to count as the HFF stop

/** A 128 KiB image carrying LSDj's 'jk' SRAM-init magic at 0x813E/0x813F — same check as lsdjSramSignature. */
function isLsdjSav(bytes: Uint8Array): boolean {
  return bytes.length >= 0x20000 && bytes[0x813e] === 0x6a && bytes[0x813f] === 0x6b;
}

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

// --- LSDj song selection (GB only) ----------------------------------------------------------------

type LsdjSav = ReturnType<typeof decodeSav>;

/** Read + decode the LSDj sav a song-selection flag needs: --sav else the sibling <rom>.sav. */
function readSav(s: Session, o: RenderOpts): { path: string; sav: LsdjSav; raw: Uint8Array } {
  const path = o.sav ?? siblingSavPath(o.rom);
  const raw = s.backend.readFile(path);
  if (!raw) throw new Error(`render: no sav to read songs from at ${path}`);
  return { path, sav: decodeSav(raw), raw };
}

/** "0: HAPPYBD, 3: DEMO" — the populated project slots, for --list-songs and not-found errors. */
function songList(sav: LsdjSav): string {
  const names = sav.projects
    .map((p, i) => (p ? `${i}: ${p.name || "(unnamed)"}` : null))
    .filter((x): x is string => x !== null);
  return names.length ? names.join(", ") : "(no named projects)";
}

/** Resolve the requested project index (by --song-index or --song name), promote it to the working
 *  song, and re-encode → the SRAM bytes to seed the system with. Returns undefined when no song flag. */
function resolveSongSeed(s: Session, o: RenderOpts): Uint8Array | undefined {
  if (o.song === undefined && o.songIndex === undefined) return undefined;
  const { sav, raw } = readSav(s, o);

  let idx: number;
  if (o.songIndex !== undefined) {
    idx = o.songIndex;
    if (!sav.projects[idx]) throw new Error(`render: slot ${idx} is empty; songs: ${songList(sav)}`);
  } else {
    const want = o.song!.trim().toUpperCase().slice(0, 8); // LSDj names are ≤8 chars, uppercase
    const hits = sav.projects
      .map((p, i) => ({ p, i }))
      .filter((x) => x.p && x.p.name.trim().toUpperCase() === want);
    if (hits.length === 0) throw new Error(`render: no song named "${o.song}"; songs: ${songList(sav)}`);
    if (hits.length > 1) console.warn(`render: ${hits.length} songs named "${o.song}"; using slot ${hits[0].i}`);
    idx = hits[0].i;
  }

  const project = sav.projects[idx]!;
  sav.workingSong = project.song; // working song and project songs share the decoded Song shape
  sav.activeProjectIndex = idx;
  console.log(`song "${project.name || "(unnamed)"}" (slot ${idx}) → working song`);
  // Seed unmodeled regions from the original sav when it's a full 128 KiB image (else author fresh).
  return encodeSav(sav, raw.length >= kSavSize ? raw : undefined);
}

/** Build the single system + project it into the DSP runtime. `seed` (LSDj song bytes) forces the adopt
 *  path; a NES split mode arms construct-time capture; otherwise addSystem auto-detects. */
function buildSystem(s: Session, o: RenderOpts, platform: Platform, seed?: Uint8Array): number {
  let id: number | null;
  if (seed) {
    // Seed the fresh system from the chosen-song SRAM bytes (adopt takes raw bytes; addSystem doesn't).
    s.project.systems.adopt({ romPath: o.rom }, { sramBytes: seed });
    syncDspFromStore(s.project, s.dsp);
    id = s.project.systems.view()[0]?.id ?? null;
  } else if (platform === "nes" && o.split !== "mix") {
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

interface StopResult {
  streams: Float32Array[]; // accumulated interleaved-stereo per stream (mix = 1 stream; split = per channel)
  startFrame: number; // frame where playback began (NR52 first on)
  stopFrame: number | null; // frame of the HFF stop (first NR52-off chunk boundary), null if capped
  hff: boolean; // an HFF stop was detected (vs. hitting --max-ms)
}

/** Render chunk-by-chunk via `renderChunk` (mix → one stream; split → one per channel), polling NR52
 *  ($FF26) after each chunk, and stop at the LSDj HFF (the APU master-enable going high→low, sustained
 *  ≥DETECT_OFF_CHUNKS). Returns each stream's full PCM + the play/stop frame markers so the caller can
 *  report the length and trim the tail. Caps at maxMs (no-HFF fallback). */
function renderUntilStop(
  s: Session,
  id: number,
  maxMs: number,
  renderChunk: (ms: number) => Float32Array[],
): StopResult {
  const chunks: Float32Array[][] = []; // [chunk][stream] of interleaved stereo
  let total = 0; // accumulated frames
  let elapsed = 0; // ms rendered
  let armed = false;
  let startFrame = 0;
  let offStreak = 0;
  let firstOffFrame: number | null = null;
  let stopFrame: number | null = null;

  while (elapsed < maxMs) {
    const chunk = renderChunk(DETECT_CHUNK_MS); // one interleaved-stereo buffer per stream
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    const frameBefore = total;
    chunks.push(chunk);
    total += chunk[0].length / 2; // streams are frame-aligned; any stream gives the frame count
    elapsed += DETECT_CHUNK_MS;

    const on = ((s.backend.readCpu(id, NR52_ADDR) ?? 0) & NR52_ON) !== 0;
    if (!armed) {
      if (on) { armed = true; startFrame = frameBefore; } // playback began here
      continue; // (still un-armed after DETECT_ARM_MS just means it hasn't started — keep going to the cap)
    }
    if (!on) {
      if (offStreak === 0) firstOffFrame = frameBefore; // the chunk the sound cut out
      if (++offStreak >= DETECT_OFF_CHUNKS) { stopFrame = firstOffFrame; break; } // sustained → HFF stop
    } else {
      offStreak = 0;
      firstOffFrame = null;
    }
  }

  const nStreams = chunks[0]?.length ?? 0;
  const streams: Float32Array[] = [];
  for (let si = 0; si < nStreams; si++) {
    const buf = new Float32Array(total * 2);
    let o = 0;
    for (const c of chunks) { buf.set(c[si], o); o += c[si].length; }
    streams.push(buf);
  }
  return { streams, startFrame, stopFrame, hff: stopFrame !== null };
}

runSession((s) => {
  const o = parseRenderArgs(hostArgs());
  const platform = platformOf(o.rom);

  // Song selection is an LSDj (GB) concept — reject it early on other platforms.
  const wantsSong = o.listSongs || o.song !== undefined || o.songIndex !== undefined;
  if (wantsSong && platform !== "gb")
    throw new Error(`render: --song/--list-songs is a Game Boy (LSDj) feature (got ${platform})`);

  // --list-songs: print the sav's populated project slots and exit before building anything.
  if (o.listSongs) {
    const { path, sav } = readSav(s, o);
    console.log(`songs in ${path}:`);
    sav.projects.forEach((p, i) => { if (p) console.log(`  ${i}: ${p.name || "(unnamed)"}`); });
    if (sav.projects.every((p) => !p)) console.log("  (no named projects — only the working song)");
    return;
  }

  // Validate split ↔ platform up front so a bad combo fails before we load anything.
  if (o.split === "pins" || o.split === "mono") {
    if (platform !== "nes") throw new Error(`render: --split ${o.split} is NES-only (got ${platform})`);
  } else if (o.split === "channels" && platform !== "gb" && platform !== "nes") {
    throw new Error(`render: --split channels needs a Game Boy or NES ROM (got ${platform})`);
  }

  // The sample rate is baked into each core at construct, so it MUST be set before buildSystem.
  if (o.sampleRate !== undefined && !s.audio.setSampleRate(o.sampleRate))
    throw new Error(`render: could not set sample rate to ${o.sampleRate}Hz`);

  const id = buildSystem(s, o, platform, resolveSongSeed(s, o));

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
    console.log(`${name}`);
  };
  const base = outBase(o);

  // LSDj length auto-detect: when a valid LSDj sav is loaded and the user didn't pin a duration, render to
  // the HFF stop (NR52→0) instead of a fixed window, report the length, and trim the silent tail.
  let lsdjLoaded = false;
  if (platform === "gb") {
    const raw = s.backend.readFile(o.sav ?? siblingSavPath(o.rom));
    lsdjLoaded = !!raw && isLsdjSav(raw);
  }
  // Auto-detect applies to both mix and split (GB channels) — split just renders per-channel chunks.
  const autoDetect = lsdjLoaded && o.start && o.ms === undefined;

  // Report the detected length + warn on a no-HFF fallback; shared by the mix and split auto-detect paths.
  const reportLength = (r: StopResult, endFrame: number) => {
    const lengthMs = Math.round(((endFrame - r.startFrame) / sr) * 1000);
    console.log(`length: ${lengthMs} ms (${endFrame - r.startFrame} frames @${sr}Hz) hff:${r.hff}`);
    if (!r.hff) console.warn(`no HFF stop within ${o.maxMs}ms — add an HFF to the song end for exact length`);
  };

  // Announce before the (possibly multi-minute) render so the CLI doesn't look hung while it works.
  const how = autoDetect ? `detecting length (HFF, cap ${o.maxMs}ms)` : `${o.ms ?? 8000}ms`;
  console.log(`rendering ${o.rom} → ${base}${o.split === "mix" ? "" : "_*"} (${how})…`);

  if (o.split === "mix") {
    if (autoDetect) {
      const r = renderUntilStop(s, id, o.maxMs, (ms) => [s.audio.renderAudio(ms)]);
      const endFrame = r.stopFrame ?? r.streams[0].length / 2;
      write(base, encodeWav(r.streams[0].subarray(0, endFrame * 2), sr, 2)); // trimmed to the HFF stop
      reportLength(r, endFrame);
      return;
    }
    const ms = o.ms ?? 8000;
    const pcm = s.audio.renderAudio(ms); // interleaved L/R float32
    write(base, encodeWav(pcm, sr, 2));
    console.log(`rendered ${o.rom} → ${base} (${ms}ms @${sr}Hz)`);
    return;
  }

  // Split render: auto-detect the HFF length (GB/LSDj) by rendering per-channel chunks + polling NR52,
  // else render a fixed window. Both yield one interleaved-stereo buffer per channel stream.
  let bufs: Float32Array[];
  if (autoDetect) {
    const r = renderUntilStop(s, id, o.maxMs, (ms) => s.audio.renderAudioPerChannel(id, ms));
    const endFrame = r.stopFrame ?? r.streams[0].length / 2;
    bufs = r.streams.map((b) => b.subarray(0, endFrame * 2)); // trimmed to the HFF stop
    reportLength(r, endFrame);
  } else {
    bufs = s.audio.renderAudioPerChannel(id, o.ms ?? 8000);
  }
  if (bufs.length === 0) throw new Error("render: renderAudioPerChannel returned no streams");

  if (platform === "gb") {
    // GB channel streams are STEREO: one stereo WAV per channel.
    bufs.forEach((b, i) => write(`${base}_${GB_CHANNELS[i] ?? `ch${i}`}.wav`, encodeWav(b, sr, 2)));
    console.log(`GB ${bufs.length}-channel render (@${sr}Hz) → ${base}_*`);
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
  console.log(`NES ${o.split} (${mono.length} streams @${sr}Hz) → ${base}_*`);
});
