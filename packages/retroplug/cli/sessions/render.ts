// The `render` CLI tool — render a ROM (+ its battery .sav / a savestate) straight to WAV from the command
// line, no script authoring, no Node. Exported as `renderTool` (a CliTool) and registered in cli/tools.ts;
// the dispatcher (cli/cli.ts, the bundle compiled into retroplug-cli) runs it under a booted session, so an
// end user with just the executable can:
//
//   retroplug-cli render <rom> [--sav f] [--state f] [--out f] [--duration t] [--sample-rate hz]
//                              [--split mix|channels|pins] [--bpm n] [--transport] [--no-start]
//
// A missing --sav auto-pairs the sibling <rom>.sav. By default it presses Start on boot so a saved song
// (e.g. LSDj) actually plays — pass --no-start to render raw boot audio. --split writes per-stream WAVs
// (no combined file): mix = one WAV (GB stereo / NES mono); channels = GB 4 stereo stems or NES 5 mono core
// channels; pins = NES 3 mono analog pins. See RENDER_HELP in renderArgs.ts for the full flag reference.
//
// LSDj length auto-detect: when a valid LSDj sav is loaded (and no --duration is pinned), render to the
// song's HFF stop (the APU master-enable NR52 going off — lsdpack's technique), report the length, and trim
// the WAV to it. --duration forces a fixed length; --max-duration caps the detection (no-HFF fallback).
//
// LSDj (GB) song selection: a .sav holds up to 32 named projects but LSDj only plays its WORKING song on
// boot, so --song NAME / --song-index N promote a chosen project to the working song before booting
// (decode → assign → re-encode → seed the fresh system). --list-songs prints the sav's song names.
import { createWavWriter, type WavWriter } from "../wav";
import { parseRenderArgs, RENDER_SUMMARY, RENDER_HELP, type RenderOpts, type SplitMode } from "../renderArgs";
import type { CliTool } from "../tools";
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
const DETECT_OFF_CHUNKS = 2; // NR52 must read off this many consecutive chunks to count as the HFF stop

/** A 128 KiB image carrying LSDj's 'jk' SRAM-init magic at 0x813E/0x813F — same check as lsdjSramSignature. */
function isLsdjSav(bytes: Uint8Array): boolean {
  return bytes.length >= 0x20000 && bytes[0x813e] === 0x6a && bytes[0x813f] === 0x6b;
}

// Per-mode stream labels, matching each system's channelLayout() order.
const GB_CHANNELS = ["pulse1", "pulse2", "wave", "noise"]; // SameBoySystem::channelLayout (stereo streams)
const NES_PINS = ["pulse", "tnd", "expansion"]; // MesenNesSystem StereoModPins (--split pins; mono streams)
const NES_CHANNELS = ["square1", "square2", "triangle", "noise", "dmc"]; // MesenNesSystem IndividualMono (--split channels; mono)

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

/** NES channelExportMode for a split mode: channels=3 (the 5 individual mono core channels),
 *  pins=1 (the 3 analog output pins). Only meaningful for NES + a non-mix split. */
function nesExportMode(split: SplitMode): number {
  return split === "channels" ? 3 : 1;
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

// --- streaming render: pump 100 ms chunks straight into per-output WavWriters (no whole-song buffer) ---

interface StopMarkers {
  startFrame: number; // frame where playback began (NR52 first on)
  endFrame: number; // frames committed to disk (== the HFF stop, or everything at the cap)
  hff: boolean; // an HFF stop was detected (vs. hitting --max-ms)
}

/** A render target: how to pull the next chunk, where each chunk's frames go (one interleaved buffer per
 *  stream — mix=1, split=N), and how to close the files. Writers are created lazily on the first chunk. */
interface RenderSink {
  renderChunk(ms: number): Float32Array[];
  emit(chunk: Float32Array[], takeFrames: number): void; // commit the first `takeFrames` frames of a chunk
  finishAll(): void; // patch every writer's header + log the outputs
}

/** Left channel of an interleaved-stereo buffer, first `frames` frames (NES streams are mono in the L lane). */
function leftLane(b: Float32Array, frames: number): Float32Array {
  const out = new Float32Array(frames);
  for (let f = 0; f < frames; f++) out[f] = b[f * 2];
  return out;
}

/** Build the sink for the requested output shape. Writers open on the first chunk once the stream count is
 *  known (from chunk.length) — opening truncates, so a re-render clobbers any stale file.
 *    mix       → 1 WAV: GB/GBA stereo, NES mono (its lanes are identical, so the left lane is lossless)
 *    channels  → GB 4 stereo stems; NES 5 mono core channels
 *    pins      → NES 3 mono analog pins
 *  (No combined multi-channel WAV — split modes write per-stream files only.) */
function buildSink(s: Session, o: RenderOpts, id: number, platform: Platform, sr: number, base: string): RenderSink {
  const renderChunk = o.split === "mix"
    ? (ms: number) => [s.audio.renderAudio(ms)]
    : (ms: number) => s.audio.renderAudioPerChannel(id, ms);

  const nesMono = platform === "nes"; // NES streams (mix + per-channel) are mono in the left lane
  let writers: WavWriter[] | null = null; // per stream (mix=1; GB channels=N stereo; NES=N mono)
  const paths: string[] = [];
  let summary = "";

  const open = (path: string, channels: number): WavWriter => {
    paths.push(path);
    return createWavWriter(s.backend, path, sr, channels);
  };

  const ensure = (streamCount: number) => {
    if (writers) return;
    if (o.split === "mix") {
      writers = [open(base, nesMono ? 1 : 2)];
      summary = `rendered ${o.rom} → ${base} (@${sr}Hz)`;
    } else if (platform === "gb") {
      writers = Array.from({ length: streamCount }, (_, i) => open(`${base}_${GB_CHANNELS[i] ?? `ch${i}`}.wav`, 2));
      summary = `GB ${streamCount}-channel render (@${sr}Hz) → ${base}_*`;
    } else {
      const names = o.split === "channels" ? NES_CHANNELS : NES_PINS;
      writers = Array.from({ length: streamCount }, (_, i) => open(`${base}_${names[i] ?? `ch${i}`}.wav`, 1));
      summary = `NES ${o.split} (${streamCount} mono streams @${sr}Hz) → ${base}_*`;
    }
  };

  return {
    renderChunk,
    emit(chunk, take) {
      ensure(chunk.length);
      if (o.split === "mix") {
        writers![0].append(nesMono ? leftLane(chunk[0], take) : chunk[0].subarray(0, take * 2));
      } else if (platform === "gb") {
        chunk.forEach((b, i) => writers![i].append(b.subarray(0, take * 2)));
      } else {
        chunk.forEach((b, i) => writers![i].append(leftLane(b, take))); // NES per-channel streams are mono
      }
    },
    finishAll() {
      if (!writers) throw new Error("render: no audio was rendered");
      writers.forEach((w) => w.finish());
      paths.forEach((p) => console.log(p));
      console.log(summary);
    },
  };
}

/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk), so the output is
 *  byte-identical to a single renderAudio(ms) of the same length for any sample rate / ms. */
function driveFixed(sink: RenderSink, targetFrames: number): void {
  let done = 0;
  while (done < targetFrames) {
    const chunk = sink.renderChunk(DETECT_CHUNK_MS);
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    const take = Math.min(chunk[0].length / 2, targetFrames - done);
    sink.emit(chunk, take);
    done += take;
  }
}

/** LSDj auto-detect render: stream chunk-by-chunk, polling NR52 ($FF26), and stop at the HFF (the APU
 *  master-enable going high→low, sustained ≥DETECT_OFF_CHUNKS). Holds back the current contiguous off-streak
 *  (≤DETECT_OFF_CHUNKS whole chunks) so committed frames end exactly at the stop; a reset flushes them in
 *  order. Caps at maxMs (no-HFF fallback → keep everything). */
function driveAutoDetect(sink: RenderSink, s: Session, id: number, maxMs: number): StopMarkers {
  let total = 0; // frames rendered (committed + held)
  let committed = 0; // frames streamed to disk
  let elapsed = 0;
  let armed = false;
  let startFrame = 0;
  const held: Float32Array[][] = []; // the current off-streak, discarded on a confirmed stop

  const commit = (chunk: Float32Array[]) => {
    const frames = chunk[0].length / 2;
    sink.emit(chunk, frames);
    committed += frames;
  };

  while (elapsed < maxMs) {
    const chunk = sink.renderChunk(DETECT_CHUNK_MS);
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    const frameBefore = total;
    total += chunk[0].length / 2;
    elapsed += DETECT_CHUNK_MS;

    const on = ((s.backend.readCpu(id, NR52_ADDR) ?? 0) & NR52_ON) !== 0;
    if (!armed) {
      commit(chunk); // keep the lead-in from frame 0
      if (on) { armed = true; startFrame = frameBefore; } // playback began here
      continue; // (still un-armed just means playback hasn't started yet — keep going to the cap)
    }
    if (!on) {
      held.push(chunk); // hold back — this may be the HFF tail
      if (held.length >= DETECT_OFF_CHUNKS) // sustained → HFF stop; discard held, end at the first off frame
        return { startFrame, endFrame: committed, hff: true };
    } else {
      for (const c of held) commit(c); // song continues: flush the (kept) off-streak in order…
      held.length = 0;
      commit(chunk); // …then this chunk
    }
  }
  for (const c of held) commit(c); // cap reached, no confirmed stop → keep everything
  return { startFrame, endFrame: committed, hff: false };
}

function runRender(s: Session, args: string[]): void {
  const o = parseRenderArgs(args);
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
  if (o.split === "pins") {
    if (platform !== "nes") throw new Error(`render: --split pins is NES-only (got ${platform})`);
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
  const base = outBase(o);

  // LSDj length auto-detect: when a valid LSDj sav is loaded and the user didn't pin a duration, render to
  // the HFF stop (NR52→0) instead of a fixed window, report the length, and trim the silent tail.
  let lsdjLoaded = false;
  if (platform === "gb") {
    const raw = s.backend.readFile(o.sav ?? siblingSavPath(o.rom));
    lsdjLoaded = !!raw && isLsdjSav(raw);
  }
  // Auto-detect applies to both mix and split (GB channels) — split just renders per-channel chunks.
  const autoDetect = lsdjLoaded && o.start && o.durationMs === undefined;

  // Report the detected length + warn on a no-HFF fallback.
  const reportLength = (m: StopMarkers) => {
    const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
    console.log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
    if (!m.hff) console.warn(`no HFF stop within ${o.maxDurationMs}ms — add an HFF to the song end for exact length`);
  };

  // Announce before the (possibly multi-minute) render so the CLI doesn't look hung while it works.
  const how = autoDetect ? `detecting length (HFF, cap ${o.maxDurationMs}ms)` : `${o.durationMs ?? 8000}ms`;
  console.log(`rendering ${o.rom} → ${base}${o.split === "mix" ? "" : "_*"} (${how})…`);

  // Stream PCM straight to the WAV files as it renders (bounded memory) rather than buffering the whole song.
  const sink = buildSink(s, o, id, platform, sr, base);
  if (autoDetect) {
    const markers = driveAutoDetect(sink, s, id, o.maxDurationMs);
    sink.finishAll();
    reportLength(markers);
  } else {
    driveFixed(sink, Math.floor(((o.durationMs ?? 8000) * sr) / 1000)); // exact target frame count
    sink.finishAll();
  }
}

/** The `render` CLI tool: name + summary for the top-level index, detailed --help, and the render body.
 *  Registered in cli/tools.ts; the dispatcher (cli/cli.ts) runs it under a booted session. */
export const renderTool: CliTool = {
  name: "render",
  summary: RENDER_SUMMARY,
  help: RENDER_HELP,
  run: runRender,
};
