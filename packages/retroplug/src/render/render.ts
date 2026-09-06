// The shared render orchestration: boot a fresh system from a ROM (+ its battery .sav / a savestate) and
// stream its audio to WAV — full mix or per-channel/per-pin stems, with tracker song-length auto-detection.
// Host-neutral: it drives a RenderContext (the CLI's Session or the worker's control plane) and reports
// through RenderHooks. The CLI `render` command (cli/sessions/render.ts) and the background render worker
// both call runRenderJob; improving this file improves both.
//
// Song-length auto-detect: when a supported tracker is loaded (and no fixed duration is pinned), render to
// the song's HFF stop, report the length, and trim the output to it. LSDj (GB) detects the stop via the APU
// master-enable NR52 going off (lsdpack's technique); risa (NES) via its sequencer flag hitting
// SEQ_MODE_STOPPED (read through the pure runtime reader). A pinned duration forces a fixed length;
// maxDurationMs caps the detection.
//
// LSDj (GB) song selection: a .sav holds up to 32 named projects but LSDj only plays its WORKING song on
// boot, so song / songIndex promote a chosen project to the working song before booting (decode → assign →
// re-encode → seed the fresh system).
import { createWavWriter, type WavWriter } from "./wav";
import { syncDspFromStore } from "../appHost";
import { extensionLower, dirname, stem, joinPath, uniqueBase } from "../pathUtil";
import { siblingSavPath } from "../savPaths";
import { decodeSav, encodeSav, kSavSize } from "../lsdj";
import { listSongs as risaListSongs, type RisaSongInfo } from "../risaSav";
import { loadSongToWorkingInSav } from "../risaSongOps";
import { listSongs as smsggdjListSongs, isSmsggdjSav } from "../smsggdj/codec/sav";
import { identifySmsggdjVersion } from "../smsggdj/romDetect";
import { resolveSmsggdjLayout } from "../smsggdj/runtime/layout";
import { isRisaRomHeader, runtime as risaRuntime } from "../risa";
import { lsdjSongCatalog, risaSongCatalog, smsggdjSongCatalog } from "../tracker";
import { smsggdjIntegration } from "../tracker/trackerIntegration"; // the leaf: the barrel exports catalogs, not integrations
import type { ChannelExportMode } from "../settingsEnums";
import {
  type Platform,
  type RenderContext,
  type RenderHooks,
  type RenderOpts,
  type RenderResult,
  type SplitMode,
  RenderCancelled,
} from "./types";

const GB_START = 7; // GameboyButton::Start — LSDj/mGB begin playback on a Start press.
const NES_START = 7; // NesButton::Start (same index) ; NES_SELECT = 6. risa plays a song on SELECT+START.
const NES_SELECT = 6;
const SMS_START = 7; // SmsButton::Start -> SmsController::Buttons::Pause. smsggdj toggles play/stop on it.
// A fixed (non-auto-detect) render runs for `durationMs` if pinned, else `maxDurationMs` — so "max duration"
// bounds every render, not just the LSDj auto-length cap. maxDurationMs defaults to 600000 (10 min) at the
// CLI / worker seams, so a render with neither pinned falls back to that cap.

// LSDj song-length auto-detect: LSDj's HFF command stops the song by powering the APU off — a 0 write to
// NR52 ($FF26), the sound master-enable register (bit 7). We poll it each render chunk and stop at the
// high→low edge (the technique lsdpack uses). Hardware-level, so version-independent + tempo/hop-agnostic.
const NR52_ADDR = 0xff26;
const NR52_ON = 0x80; // bit 7 = all-sound-on
const DETECT_CHUNK_MS = 100; // poll granularity (≈ detection precision)
const DETECT_OFF_CHUNKS = 2; // NR52 must read off this many consecutive chunks to count as the HFF stop

/** A 128 KiB image carrying LSDj's 'jk' SRAM-init magic at 0x813E/0x813F — same check as lsdjSramSignature. */
export function isLsdjSav(bytes: Uint8Array): boolean {
  return bytes.length >= 0x20000 && bytes[0x813e] === 0x6a && bytes[0x813f] === 0x6b;
}

// Per-mode stream labels, matching each system's channelLayout() order.
const GB_CHANNELS = ["pulse1", "pulse2", "wave", "noise"]; // SameBoySystem::channelLayout (stereo streams)
const NES_PINS = ["pulse", "tnd", "expansion"]; // MesenNesSystem StereoModPins (--split pins; mono streams)
const NES_CHANNELS = ["square1", "square2", "triangle", "noise", "dmc"]; // MesenNesSystem IndividualMono (--split channels; mono)

export function platformOf(rom: string): Platform {
  switch (extensionLower(rom)) {
    case ".gb":
    case ".gbc": return "gb";
    case ".nes": return "nes";
    case ".gba": return "gba";
    case ".sms": return "sms";
    case ".gg": return "gg";
    default: return "other";
  }
}

/** NES channelExportMode for a split mode: channels → the 5 individual mono core channels,
 *  pins → the 3 analog output pins. Only meaningful for NES + a non-mix split. */
function nesExportMode(split: SplitMode): ChannelExportMode {
  return split === "channels" ? "individualMono" : "stereoModPins";
}

/** True when `o.rom` is a risa cart (iNES header fingerprint) — gates the risa play gesture so generic NES
 *  ROMs are left untouched (their audio, if any, plays without a synthetic button press). */
function isRisaRom(ctx: RenderContext, o: RenderOpts): boolean {
  const bytes = ctx.backend.readFile(o.rom);
  return !!bytes && isRisaRomHeader(bytes.subarray(0, 16));
}

/** risa begins song playback on SELECT+START (a plain START first nudges it off an empty phrase context —
 *  the sequence the runtime-reader test proved reliable). The two prep renders are pre-song and discarded;
 *  the SELECT+START render — where the song begins — is CAPTURED through the sink and returned as the
 *  recording's lead-in, so the opening frames aren't lost. */
function pressRisaPlay(ctx: RenderContext, sink: RenderSink, id: number): Float32Array[][] {
  ctx.audio.pressButton(id, NES_START, true);
  sink.renderChunk(DETECT_CHUNK_MS); // prep: nudge off an empty phrase context (pre-song, discarded)
  ctx.audio.pressButton(id, NES_START, false);
  sink.renderChunk(DETECT_CHUNK_MS); // prep (discarded)
  ctx.audio.pressButton(id, NES_SELECT, true);
  ctx.audio.pressButton(id, NES_START, true);
  const head = sink.renderChunk(DETECT_CHUNK_MS); // the song starts here — capture it as the lead-in
  ctx.audio.pressButton(id, NES_START, false);
  ctx.audio.pressButton(id, NES_SELECT, false);
  return [head];
}

type Logger = (msg: string) => void;

// --- LSDj song selection (GB only) ----------------------------------------------------------------

type LsdjSav = ReturnType<typeof decodeSav>;

/** Read + decode the LSDj sav a song-selection flag needs: the given sav else the sibling <rom>.sav. */
export function readSav(ctx: RenderContext, o: RenderOpts): { path: string; sav: LsdjSav; raw: Uint8Array } {
  const path = o.sav ?? siblingSavPath(o.rom);
  const raw = ctx.backend.readFile(path);
  if (!raw) throw new Error(`render: no sav to read songs from at ${path}`);
  return { path, sav: decodeSav(raw), raw };
}

/** "0: HAPPYBD, 3: DEMO" — the populated project slots, for --list-songs and not-found errors. */
export function songList(sav: LsdjSav): string {
  const names = sav.projects
    .map((p, i) => (p ? `${i}: ${p.name || "(unnamed)"}` : null))
    .filter((x): x is string => x !== null);
  return names.length ? names.join(", ") : "(no named projects)";
}

/** A chosen-song seed (raw SRAM promoting the catalog song to the working song) plus the selected song's
 *  name — the name defaults the output filename when --song/--song-index is used. */
interface SongSeed {
  seed: Uint8Array;
  name: string | null;
}

/** Resolve a song-selection flag to the SRAM bytes that seed the fresh system with the chosen catalog song
 *  promoted to the working song. Platform-aware: GB → LSDj projects, NES → risa catalog. Undefined when no
 *  song flag is set. */
function resolveSongSeed(ctx: RenderContext, o: RenderOpts, log: Logger, warn: Logger): SongSeed | undefined {
  if (o.song === undefined && o.songIndex === undefined) return undefined;
  const platform = platformOf(o.rom);
  if (platform === "gb") return resolveLsdjSongSeed(ctx, o, log, warn);
  if (platform === "nes") return resolveRisaSongSeed(ctx, o, log, warn);
  // sms/gg is deliberately absent: smsggdj cannot be seeded through SRAM (see resolveSmsggdjSong), so
  // runRenderJob resolves and applies it separately, after the boot settle.
  if (platform === "sms" || platform === "gg") return undefined;
  throw new Error(
    `render: --song/--song-index is a Game Boy (LSDj) / NES (risa) / Master System + Game Gear (smsggdj) feature (got ${platform})`,
  );
}

function resolveLsdjSongSeed(ctx: RenderContext, o: RenderOpts, log: Logger, warn: Logger): SongSeed {
  const { sav, raw } = readSav(ctx, o);
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
    if (hits.length > 1) warn(`render: ${hits.length} songs named "${o.song}"; using slot ${hits[0].i}`);
    idx = hits[0].i;
  }

  const project = sav.projects[idx]!;
  sav.workingSong = project.song; // working song and project songs share the decoded Song shape
  sav.activeProjectIndex = idx;
  log(`song "${project.name || "(unnamed)"}" (slot ${idx}) → working song`);
  // Seed unmodeled regions from the original sav when it's a full 128 KiB image (else author fresh).
  return { seed: encodeSav(sav, raw.length >= kSavSize ? raw : undefined), name: project.name || null };
}

// --- risa (NES) song selection ---------------------------------------------------------------------

/** Read the raw risa battery a song flag needs (the given sav else the sibling <rom>.sav). */
export function readRisaSav(ctx: RenderContext, o: RenderOpts): { path: string; raw: Uint8Array } {
  const path = o.sav ?? siblingSavPath(o.rom);
  const raw = ctx.backend.readFile(path);
  if (!raw) throw new Error(`render: no sav to read songs from at ${path}`);
  return { path, raw };
}

/** The risa catalog's saved songs (index + name), for --list-songs. */
export function readRisaSongs(ctx: RenderContext, o: RenderOpts): { path: string; songs: RisaSongInfo[] } {
  const { path, raw } = readRisaSav(ctx, o);
  return { path, songs: risaListSongs(raw) };
}

const risaSongList = (songs: RisaSongInfo[]): string =>
  songs.length ? songs.map((sg) => `${sg.index}: ${sg.name || "(unnamed)"}`).join(", ") : "(no saved songs)";

function resolveRisaSongSeed(ctx: RenderContext, o: RenderOpts, log: Logger, warn: Logger): SongSeed {
  const { raw } = readRisaSav(ctx, o);
  const songs = risaListSongs(raw);
  let idx: number;
  if (o.songIndex !== undefined) {
    idx = o.songIndex;
    if (!songs.some((sg) => sg.index === idx)) throw new Error(`render: slot ${idx} is empty; songs: ${risaSongList(songs)}`);
  } else {
    const want = o.song!.trim().toUpperCase();
    const hits = songs.filter((sg) => sg.name.trim().toUpperCase() === want);
    if (hits.length === 0) throw new Error(`render: no song named "${o.song}"; songs: ${risaSongList(songs)}`);
    if (hits.length > 1) warn(`render: ${hits.length} songs named "${o.song}"; using slot ${hits[0].index}`);
    idx = hits[0].index;
  }
  // Promote the catalog song into the working banks (0-3), keeping the catalog (banks 4-7) intact. Requires a
  // current-layout catalog — the firmware migrates legacy on boot, so a live-read battery is always current.
  const seed = loadSongToWorkingInSav(raw, idx);
  if (!seed) throw new Error(`render: could not load risa song ${idx} (needs a current-layout catalog)`);
  const name = songs.find((sg) => sg.index === idx)?.name || null;
  log(`song "${name || "(unnamed)"}" (slot ${idx}) → working song`);
  return { seed, name };
}

/** The smsggdj catalog's saved songs (index + name), for --list-songs. */
export function readSmsggdjSongs(ctx: RenderContext, o: RenderOpts): { path: string; songs: { index: number; name: string }[] } {
  const { path, raw } = readRisaSav(ctx, o); // sibling-sav resolution, console-agnostic despite the name
  return { path, songs: smsggdjListSongs(raw) };
}

/** smsggdj: unlike LSDj and risa, the seed does NOT move the song anywhere. This cart's working song is
 *  work RAM and it boots blank on purpose, so the save NAMES the slot and the cart loads it on the way
 *  up - the same mechanism the Songs menu uses. See src/tracker/smsggdjSongCatalog. */
// --- smsggdj (SMS / GG) song selection --------------------------------------------------------------
//
// Unlike LSDj and risa this CANNOT be a seed. Those two keep the working song in the battery, so writing
// the chosen song into the SRAM the fresh system boots from is enough. smsggdj's working song is the live
// work-RAM block at $C000 and the cart boots deliberately blank (`song_new`, main.asm:238) - there is
// nothing in the image to promote. An earlier version of this seeded the superblock's `cur_slot` byte,
// which is the right durable record but only a v0.46+ cart reads it at boot, so on the shipped v0.45 it
// selected nothing and the render came out silent. Selection therefore resolves to an INDEX here and the
// song is written into the booted core by loadSmsggdjSongLive, the same path the Songs menu Load uses.

/** True when `o.rom` is an smsggdj cart. Gates the play press, the song load and the no-song warning, so a
 *  generic Master System game is left alone - on SMS the play button IS the Pause NMI, and pressing it on
 *  an ordinary game would pause it. */
function isSmsggdjRom(ctx: RenderContext, o: RenderOpts): boolean {
  const bytes = ctx.backend.readFile(o.rom);
  return !!bytes && identifySmsggdjVersion(bytes) !== null;
}

/** Is the smsggdj cart's transport running? Null when it cannot be told (no layout, no RAM snapshot) —
 *  the caller then behaves as it did before, which is to press play. */
function smsggdjIsPlaying(ctx: RenderContext, o: RenderOpts, id: number): boolean | null {
  const rom = ctx.backend.readFile(o.rom);
  const layout = rom ? resolveSmsggdjLayout(identifySmsggdjVersion(rom)) : null;
  const ram = layout ? ctx.backend.readRam(id) : null;
  if (!layout || !ram || ram.length <= layout.playState) return null;
  return ram[layout.playState] !== 0;
}

/** The chosen smsggdj song: which slot, and its name for the output filename. */
interface SmsggdjSongPick {
  index: number;
  name: string | null;
}

function resolveSmsggdjSong(ctx: RenderContext, o: RenderOpts, log: Logger, warn: Logger): SmsggdjSongPick {
  const { raw } = readRisaSav(ctx, o); // the sibling-sav resolution is console-agnostic despite the name
  const songs = smsggdjListSongs(raw);
  const listing = songs.map((sg) => `${sg.index}:${sg.name}`).join(", ") || "(none)";
  let idx: number;
  if (o.songIndex !== undefined) {
    idx = o.songIndex;
    if (!songs.some((sg) => sg.index === idx)) throw new Error(`render: slot ${idx} is empty; songs: ${listing}`);
  } else {
    const want = o.song!.trim().toUpperCase();
    const hits = songs.filter((sg) => sg.name.trim().toUpperCase() === want);
    if (hits.length === 0) throw new Error(`render: no song named "${o.song}"; songs: ${listing}`);
    if (hits.length > 1) warn(`render: ${hits.length} songs named "${o.song}"; using slot ${hits[0].index}`);
    idx = hits[0].index;
  }
  const name = songs.find((sg) => sg.index === idx)?.name || null;
  log(`song "${name || "(unnamed)"}" (slot ${idx}) → work RAM`);
  return { index: idx, name };
}

// The load has to land AFTER the cart's boot-time `song_new`, which blanks the song block on the way up
// (main.asm:224, right after the splash). The shared 1500 ms settle is tuned for the GB splash and is not
// enough here: measured on the shipped v0.45, a write at 1500 or 2000 ms is silently undone and only
// 2500 ms and later survive. A bigger fixed number would be guessing - the margin is ~500 ms on NTSC and
// PAL runs ~17% slower - and simply retrying until the block reads back is NOT enough either: measured,
// a write at 1750 ms verifies perfectly (the whole 6,912-byte block byte-identical) and is then wiped
// 400 ms later, so the loop stops on a match that does not last.
//
// So wait for the cart to tell us it is past that point. `song_new` fills wave_ram with the 8 preset
// waves, and work RAM powers on all zeros (MesenSmsSystem pins RamPowerOnState = AllZeros), so a
// non-zero wave_ram IS "song_new has run". The write then still has to verify, and hold across several
// checks - the readback is the guarantee, the boot signal is what stops it passing too early.
const SMSGGDJ_STEP_MS = 250;
const SMSGGDJ_BOOT_STEPS = 24; // x STEP = 6s of boot to wait through before writing
const SMSGGDJ_HOLD_STEPS = 4; // consecutive verified checks (1s) before the load is believed
const SMSGGDJ_WRITE_TRIES = 4;

/** Any non-zero byte in `ram[at .. at+len)`. */
function anyNonZero(ram: Uint8Array, at: number, len: number): boolean {
  for (let i = at; i < at + len && i < ram.length; i++) if (ram[i] !== 0) return true;
  return false;
}

/** `ram[at .. at+len) === want[from .. from+len)`. */
function ramMatches(ram: Uint8Array, at: number, want: Uint8Array, from: number, len: number): boolean {
  if (ram.length < at + len || want.length < from + len) return false;
  for (let i = 0; i < len; i++) if (ram[at + i] !== want[from + i]) return false;
  return true;
}

/** Write the chosen song into the booted cart's work RAM and confirm it stuck (see above). Returns having
 *  verified the song is really in memory and stable.
 *
 *  Throws rather than warns, on every failure path. The user named a song; rendering the blank boot song
 *  instead would produce a silent WAV that looks like a successful render - exactly the bug this whole
 *  path exists to fix. */
function loadSmsggdjSongLive(ctx: RenderContext, o: RenderOpts, id: number, index: number): void {
  const rom = ctx.backend.readFile(o.rom);
  const sav = ctx.backend.readFile(o.sav ?? siblingSavPath(o.rom));
  if (!rom || !sav) throw new Error(`render: could not re-read the ROM/sav to load smsggdj song ${index}`);
  const version = identifySmsggdjVersion(rom);
  const layout = resolveSmsggdjLayout(version);
  const writes = layout ? smsggdjIntegration.liveLoad?.(rom, sav, index, ctx.backend.readRam(id) ?? undefined) : null;
  if (!layout || !writes?.length) {
    throw new Error(
      `render: could not load smsggdj song ${index} - no work-RAM layout for this build ` +
        `(${version ?? "unrecognized version"}), or the song will not decode`,
    );
  }
  const want = writes.find((w) => w.offset === layout.song)?.bytes;
  if (!want) throw new Error(`render: smsggdj song ${index} produced no song block to write`);

  // wave_ram is the block's first field, so it spans [song, song + phrasePool) - no extra symbol needed.
  for (let i = 0; i < SMSGGDJ_BOOT_STEPS; i++) {
    const ram = ctx.backend.readRam(id);
    if (ram && anyNonZero(ram, layout.song, layout.phrasePool)) break;
    ctx.audio.renderAudio(SMSGGDJ_STEP_MS);
  }

  // The whole block is compared, not a witness field: nothing else writes it while the transport is
  // stopped, and a partial match is exactly the failure being guarded against.
  for (let tries = 0; tries < SMSGGDJ_WRITE_TRIES; tries++) {
    for (const w of writes) {
      if (!ctx.backend.writeRam(id, w.offset, w.bytes))
        throw new Error(`render: work-RAM write refused at +$${w.offset.toString(16)} loading smsggdj song ${index}`);
    }
    let held = 0;
    while (held < SMSGGDJ_HOLD_STEPS) {
      ctx.audio.renderAudio(SMSGGDJ_STEP_MS); // let the queued writes land, and the boot move on
      const ram = ctx.backend.readRam(id);
      if (!ram || !ramMatches(ram, layout.song, want, 0, want.length)) break;
      held++;
    }
    if (held === SMSGGDJ_HOLD_STEPS) return;
  }
  throw new Error(
    `render: smsggdj song ${index} would not stay in work RAM - the cart kept overwriting it (still booting?)`,
  );
}

/** Warn when an smsggdj render was given no song to play. LSDj and risa boot into their working song, so a
 *  bare `render cart.gb` makes music; this cart boots blank ON PURPOSE (`song_new`, so a first power-on
 *  still makes a sound) and there is no working song in the battery to fall back on. Without --song the
 *  output is therefore silence, which used to happen with no explanation at all. Not an error: rendering
 *  the boot state is legitimate (it is what --no-start does elsewhere), and a `.sav` may hold no songs. */
function warnIfSmsggdjHasNoSong(ctx: RenderContext, o: RenderOpts, platform: Platform, warn: Logger): void {
  if (platform !== "sms" && platform !== "gg") return;
  // A savestate carries work RAM, so it carries the working song - that is exactly how the UI's
  // System > Render reaches this console, and warning there would be false.
  if (o.state) return;
  if (!isSmsggdjRom(ctx, o)) return;
  const raw = ctx.backend.readFile(o.sav ?? siblingSavPath(o.rom));
  if (!raw || !isSmsggdjSav(raw)) return;
  const songs = smsggdjListSongs(raw);
  if (!songs.length) return;
  warn(
    `render: smsggdj boots to a BLANK song (it keeps no working song in the battery), so this will render ` +
      `silence — pass --song <name> or --song-index <n> to play one of: ` +
      songs.map((sg) => `${sg.index}:${sg.name || "(unnamed)"}`).join(", "),
  );
}

/** Build the single system + project it into the DSP runtime. `seed` (LSDj song bytes) forces the adopt
 *  path; a NES split mode arms construct-time capture; otherwise addSystem auto-detects. */
function buildSystem(ctx: RenderContext, o: RenderOpts, platform: Platform, seed?: Uint8Array): number {
  let id: number | null;
  const nesSplit = platform === "nes" && o.split !== "mix";
  if (seed || nesSplit) {
    // A chosen-song seed (adopt takes raw SRAM bytes) and NES per-channel capture (channelExportMode engages
    // at construct/onActivate) both go through adopt — combined here so `--song-index … --split channels`
    // arms both. A seed replaces the sav; without one, pair the sav. adopt is quiet → project the store by
    // hand (bootSession's onSystemsChange hook doesn't fire).
    const spec: Parameters<typeof ctx.project.systems.adopt>[0] = { romPath: o.rom };
    if (!seed && o.sav) spec.savPath = o.sav;
    if (nesSplit) spec.roles = [{ kind: "mesen", config: { channelExportMode: nesExportMode(o.split) } }];
    ctx.project.systems.adopt(spec, seed ? { sramBytes: seed } : undefined);
    syncDspFromStore(ctx.project, ctx.dsp);
    id = ctx.project.systems.view()[0]?.id ?? null;
  } else {
    // mix (any platform) + GB channels: addSystem auto-detects roles and — via bootSession's
    // onSystemsChange hook — projects into the DSP runtime. Sibling <rom>.sav auto-pairs when sav absent.
    id = ctx.project.systems.addSystem(o.rom, { explicitSav: o.sav });
  }
  if (id == null) throw new Error(`render: could not load ROM: ${o.rom}`);
  if (o.state && ctx.project.systems.loadState(id, o.state) == null)
    throw new Error(`render: could not load savestate: ${o.state}`);
  return id;
}

/** The working song's name for a tracker cart (LSDj/risa), read straight from the sav — defaults the output
 *  filename when neither --out nor a --song selection is given. Null for a non-tracker ROM / no sav (→ ROM
 *  name). Uses the same catalog readers the UI's recents label + renderBaseName use. */
function currentSongName(ctx: RenderContext, o: RenderOpts, platform: Platform): string | null {
  const raw = ctx.backend.readFile(o.sav ?? siblingSavPath(o.rom));
  if (!raw) return null;
  if (platform === "gb") return isLsdjSav(raw) ? lsdjSongCatalog.workingName(raw) : null;
  if (platform === "nes") return risaSongCatalog.workingName(raw); // self-guards on the N8T magic
  // smsggdj answers from the superblock's cur_slot with no work RAM to consult, which is null on every
  // build before v0.46 — so this falls through to the ROM stem there, as it did before.
  if (platform === "sms" || platform === "gg") return smsggdjSongCatalog.workingName(raw);
  return null;
}

// A safe filename fragment for a song-derived output name (mirrors the CLI sanitize in lsdj-rom/risa-rom).
const sanitizeRenderName = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_");

/** The WAV output base. With --out: the given path (mix) or a prefix (split, a trailing .wav trimmed).
 *  Without it: the working/selected SONG name for a tracker cart (LSDj/risa) else the ROM stem, written next
 *  to the ROM — so `songName === null` is byte-identical to the old ROM-derived default. Exported for tests. */
export function outBase(o: RenderOpts, songName: string | null): string {
  if (o.out) return o.split === "mix" ? o.out : o.out.toLowerCase().endsWith(".wav") ? o.out.slice(0, -4) : o.out;
  const clean = songName ? sanitizeRenderName(songName) : "";
  const base = joinPath(dirname(o.rom), clean || stem(o.rom));
  return o.split === "mix" ? `${base}.wav` : base;
}

/** The WAV file paths a render will write for `prefix` (a base WITHOUT the extension), mirroring buildSink's
 *  naming: mix → one `<prefix>.wav`; split → one `<prefix>_<channel>.wav` per stream. Used by the "rename"
 *  policy to detect a collision on any of the mode's outputs. */
function renderOutputPaths(o: RenderOpts, platform: Platform, prefix: string): string[] {
  if (o.split === "mix") return [`${prefix}.wav`];
  const names = platform === "gb" ? GB_CHANNELS : o.split === "channels" ? NES_CHANNELS : NES_PINS;
  return names.map((n) => `${prefix}_${n}.wav`);
}

/** Apply the "If Exists" policy to a resolved base. "overwrite" (default) clobbers; "rename" bumps to the
 *  first `<name>_N` whose outputs don't yet exist (checking every file the mode would write). */
function resolveOnExists(ctx: RenderContext, o: RenderOpts, platform: Platform, base: string): string {
  if (o.onExists !== "rename") return base;
  const isMix = o.split === "mix";
  const prefix = isMix && base.toLowerCase().endsWith(".wav") ? base.slice(0, -4) : base;
  const unique = uniqueBase(prefix, (p) => renderOutputPaths(o, platform, p).some((f) => ctx.backend.fileExists(f)));
  return isMix ? `${unique}.wav` : unique;
}

// --- streaming render: pump 100 ms chunks straight into per-output WavWriters (no whole-song buffer) ---

interface StopMarkers {
  startFrame: number; // frame where playback began (NR52 first on)
  endFrame: number; // frames committed to disk (== the HFF stop, or everything at the cap)
  hff: boolean; // an HFF stop was detected (vs. hitting maxMs)
}

/** A render target: how to pull the next chunk, where each chunk's frames go (one interleaved buffer per
 *  stream — mix=1, split=N), and how to close the files. Writers are created lazily on the first chunk. */
interface RenderSink {
  renderChunk(ms: number): Float32Array[];
  emit(chunk: Float32Array[], takeFrames: number): void; // commit the first `takeFrames` frames of a chunk
  finishAll(): string[]; // patch every writer's header, log + return the output paths
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
function buildSink(ctx: RenderContext, o: RenderOpts, id: number, platform: Platform, sr: number, base: string, log: Logger): RenderSink {
  const renderChunk = o.split === "mix"
    ? (ms: number) => [ctx.audio.renderAudio(ms)]
    : (ms: number) => ctx.audio.renderAudioPerChannel(id, ms);

  const nesMono = platform === "nes"; // NES streams (mix + per-channel) are mono in the left lane
  let writers: WavWriter[] | null = null; // per stream (mix=1; GB channels=N stereo; NES=N mono)
  const paths: string[] = [];
  let summary = "";

  const open = (path: string, channels: number): WavWriter => {
    paths.push(path);
    return createWavWriter(ctx.backend, path, sr, channels);
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
      paths.forEach((p) => log(p));
      log(summary);
      return paths.slice();
    },
  };
}

/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk), so the output is
 *  byte-identical to a single renderAudio(ms) of the same length for any sample rate / ms. `leadIn` is the
 *  captured play-gesture audio (the song's opening frames) — committed first so the recording's head isn't
 *  lost, trimmed against the target like any other chunk. */
function driveFixed(sink: RenderSink, targetFrames: number, sr: number, hooks: RenderHooks, leadIn: Float32Array[][] = []): void {
  let done = 0;
  const emit = (chunk: Float32Array[]) => {
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    const take = Math.min(chunk[0].length / 2, targetFrames - done);
    if (take > 0) { sink.emit(chunk, take); done += take; }
  };
  for (const chunk of leadIn) { if (done >= targetFrames) break; emit(chunk); }
  while (done < targetFrames) {
    if (hooks.isCancelled?.()) throw new RenderCancelled();
    emit(sink.renderChunk(DETECT_CHUNK_MS));
    hooks.onRendered?.((done / sr) * 1000); // committed audio so far (the last chunk is trimmed to the target)
  }
}

/** Length auto-detect render: stream chunk-by-chunk, polling `isPlaying` (LSDj's NR52 / risa's seq_mode),
 *  and stop at the HFF (the probe going true→false, sustained ≥DETECT_OFF_CHUNKS). Holds back the current
 *  contiguous off-streak (≤DETECT_OFF_CHUNKS whole chunks) so committed frames end exactly at the stop; a
 *  reset flushes them in order. Caps at maxMs (no-HFF fallback → keep everything). */
function driveAutoDetect(sink: RenderSink, isPlaying: () => boolean, maxMs: number, sr: number, hooks: RenderHooks, leadIn: Float32Array[][] = []): StopMarkers {
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

  // The captured play gesture: the song began on the Start press, so commit its frames from 0 and arm now
  // (the HFF tail-detection below still ends the song at the right place). startFrame stays 0 — the whole
  // committed output is song, so the reported length includes this head.
  for (const chunk of leadIn) {
    commit(chunk);
    total += chunk[0].length / 2;
    elapsed += DETECT_CHUNK_MS;
    armed = true;
  }

  while (elapsed < maxMs) {
    if (hooks.isCancelled?.()) throw new RenderCancelled();
    const chunk = sink.renderChunk(DETECT_CHUNK_MS);
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    const frameBefore = total;
    total += chunk[0].length / 2;
    elapsed += DETECT_CHUNK_MS;
    // Rendered audio so far, counting the held off-streak: monotonic, and at most DETECT_OFF_CHUNKS ahead
    // of what lands on disk (a confirmed HFF stop discards those held chunks).
    hooks.onRendered?.((total / sr) * 1000);

    const on = isPlaying();
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

/** The "is the song still playing" probe for length auto-detect, or null when `o.rom` isn't a supported
 *  tracker (so the render uses a fixed duration instead). LSDj (GB) reads NR52 — the APU master-enable an
 *  HFF powers off; risa (NES) reads seq_mode through the pure runtime reader — SEQ_MODE_STOPPED when the
 *  last track HFFs. Both go true while sounding, false at the author's HFF stop. */
function buildPlayingProbe(ctx: RenderContext, o: RenderOpts, id: number, platform: Platform, warn: Logger): (() => boolean) | null {
  if (platform === "gb") {
    const raw = ctx.backend.readFile(o.sav ?? siblingSavPath(o.rom));
    if (!raw || !isLsdjSav(raw)) return null;
    return () => ((ctx.backend.readCpu(id, NR52_ADDR) ?? 0) & NR52_ON) !== 0;
  }
  if (platform === "nes") {
    const rom = ctx.backend.readFile(o.rom);
    if (!rom || !isRisaRomHeader(rom.subarray(0, 16))) return null; // generic NES ROMs: no end-detect
    const version = risaRuntime.identifyRisaVersion(rom);
    const layout = risaRuntime.resolveRisaLayout(version);
    if (!layout) {
      // A risa cart whose version has no internal-RAM layout: the reader can't find seq_mode, so we fall
      // back to a fixed render — but say why, or it just looks like the song never stops (it can't be seen
      // to stop). Only these versions expose the addresses song-length auto-detect needs.
      warn(`render: risa ${version ?? "(unrecognized version)"} has no runtime layout — song-length ` +
        `auto-detect unavailable (supported: ${risaRuntime.supportedRisaVersions().join(", ")}); ` +
        `rendering a fixed ${o.durationMs ?? o.maxDurationMs}ms`);
      return null;
    }
    // readRam is the per-block WRAM snapshot (safe while the core plays). If it's momentarily unavailable,
    // report "playing" so a read gap never trims the song early — the maxMs cap still bounds the render.
    return () => {
      const ram = ctx.backend.readRam(id);
      return ram ? risaRuntime.decodeRisaState(ram, layout).playing : true;
    };
  }
  return null;
}

/** Validate a resolved render request against its platform. Throws on a bad split/platform or song/platform
 *  combo so the caller fails before loading anything. (Shared by the CLI and the worker.) */
export function validateRenderOpts(o: RenderOpts, platform: Platform): void {
  // Song selection promotes a saved catalog song to the working song — LSDj (GB), risa (NES) and smsggdj
  // (SMS/GG). smsggdj is not a seed but a post-boot work-RAM write; the flag is accepted all the same.
  const songPlatforms: Platform[] = ["gb", "nes", "sms", "gg"];
  if ((o.song !== undefined || o.songIndex !== undefined) && !songPlatforms.includes(platform))
    throw new Error(
      `render: --song/--song-index is a Game Boy (LSDj) / NES (risa) / Master System + Game Gear (smsggdj) feature (got ${platform})`,
    );
  if (o.split === "pins") {
    if (platform !== "nes") throw new Error(`render: --split pins is NES-only (got ${platform})`);
  } else if (o.split === "channels" && platform !== "gb" && platform !== "nes") {
    throw new Error(`render: --split channels needs a Game Boy or NES ROM (got ${platform})`);
  }
}

/** Auto-start playback and CAPTURE the song's opening frames through the sink. The song begins the moment
 *  Start is pressed, so the button-hold render (needed to register the press) must be captured as the
 *  recording's head — not discarded, which drops the first ~100 ms. Returns the captured lead-in chunks (in
 *  the sink's mix/split shape); empty when o.start is off or the platform has no play gesture. */
function pressPlay(ctx: RenderContext, sink: RenderSink, o: RenderOpts, id: number, platform: Platform): Float32Array[][] {
  if (!o.start) return [];
  if (platform === "gb") {
    // LSDj/mGB begin on the Start press; hold it across one captured chunk to register the press, then
    // release (Start is a play toggle — the song keeps running once the release lands next block).
    ctx.audio.pressButton(id, GB_START, true);
    const head = sink.renderChunk(DETECT_CHUNK_MS);
    ctx.audio.pressButton(id, GB_START, false);
    return [head];
  }
  if (platform === "nes" && isRisaRom(ctx, o)) return pressRisaPlay(ctx, sink, id); // generic NES ROMs untouched
  if ((platform === "sms" || platform === "gg") && isSmsggdjRom(ctx, o)) {
    // The cart's play/stop control is the PAUSE button (SMS: the Z80 NMI; GG: its real Start): it sets
    // nmi_event and the frame loop turns that into tp_start when stopped, tp_stop when playing
    // (main.asm:928-936). From the boot SONG screen tp_start is engine_play in MODE_SONG - the whole song
    // from song_cur, which is what a render wants. Gated on the cart being smsggdj: on a generic SMS game
    // this button pauses it.
    //
    // It is a TOGGLE, so pressing it blind is wrong whenever the cart is already running - which a render
    // restored from a savestate (the UI's System > Render carries one, so the render starts from what you
    // were hearing) very much can be. Ask first.
    if (smsggdjIsPlaying(ctx, o, id) === true) return [];
    ctx.audio.pressButton(id, SMS_START, true);
    const head = sink.renderChunk(DETECT_CHUNK_MS); // the song starts here — capture it as the lead-in
    ctx.audio.pressButton(id, SMS_START, false);
    return [head];
  }
  return [];
}

/** Boot a fresh system from the request and stream its audio to WAV. Returns the written paths (+ the
 *  detected length for an LSDj auto-detect render). Host-neutral — the CLI and the render worker both call
 *  this; hooks default to console logging with no progress/cancel. Does NOT handle --list-songs (a query,
 *  not a render — the CLI wrapper handles that). */
export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHooks = {}): RenderResult {
  const log = hooks.log ?? ((m: string) => console.log(m));
  const warn = hooks.warn ?? ((m: string) => console.warn(m));
  const platform = platformOf(o.rom);

  validateRenderOpts(o, platform);

  // The sample rate is baked into each core at construct, so it MUST be set before buildSystem.
  if (o.sampleRate !== undefined && !ctx.audio.setSampleRate(o.sampleRate))
    throw new Error(`render: could not set sample rate to ${o.sampleRate}Hz`);

  const seed = resolveSongSeed(ctx, o, log, warn);
  // smsggdj resolves its song here rather than through the seed, because its working song is not in the
  // battery — see resolveSmsggdjSong. Resolved BEFORE the boot so a bad --song fails fast.
  const smsSong =
    (platform === "sms" || platform === "gg") && (o.song !== undefined || o.songIndex !== undefined)
      ? resolveSmsggdjSong(ctx, o, log, warn)
      : undefined;
  const id = buildSystem(ctx, o, platform, seed?.seed);

  ctx.audio.renderAudio(1500); // settle boot (past the mGB/LSDj splash) before driving playback
  if (o.bpm) ctx.audio.setBpm(o.bpm);
  if (o.transport) ctx.audio.setTransport(true);

  // AFTER the settle: the cart's boot blanks the song block, so an earlier write would be wiped.
  if (smsSong) loadSmsggdjSongLive(ctx, o, id, smsSong.index);
  else warnIfSmsggdjHasNoSong(ctx, o, platform, warn);

  const sr = ctx.audio.sampleRate();
  // Default the filename to the SONG: the selected song when --song/--song-index is used, else the sav's
  // working song. --out overrides both, so skip the sav read then.
  const songName = o.out ? null : seed ? seed.name : smsSong ? smsSong.name : currentSongName(ctx, o, platform);
  const base = resolveOnExists(ctx, o, platform, outBase(o, songName));

  // Song-length auto-detect: when the ROM is a supported tracker (LSDj on GB, risa on NES) and the user
  // didn't pin a duration, render to the HFF stop instead of a fixed window, report the length, and trim
  // the silent tail. buildPlayingProbe returns null for anything else → a fixed render.
  const isPlaying = buildPlayingProbe(ctx, o, id, platform, warn);
  // Auto-detect applies to both mix and split (GB channels) — split just renders per-channel chunks.
  const autoDetect = isPlaying !== null && o.start && o.durationMs === undefined;

  // smsggdj has no song end to detect, so a render with no --duration silently ran for the whole 10-minute
  // cap. That is not a missing probe we could add: `H` is a phrase hop, not LSDj's HFF stop, nothing in the
  // command set touches the transport, and when a chain ends the track loops back to the top of its own
  // contiguous block (engine.asm at_loopblk) - columns loop independently and forever, by design. Say so
  // instead of leaving the user to guess why the WAV is exactly the cap long.
  if (!autoDetect && o.durationMs === undefined && isSmsggdjRom(ctx, o)) {
    warn(
      `render: smsggdj songs loop forever (no HFF-style stop exists on this console), so song-length ` +
        `auto-detect is not possible — rendering the full ${o.maxDurationMs}ms cap; pass --duration to pin a length`,
    );
  }

  // Announce before the (possibly multi-minute) render so callers don't look hung while it works.
  const how = autoDetect ? `detecting length (HFF, cap ${o.maxDurationMs}ms)` : `${o.durationMs ?? o.maxDurationMs}ms`;
  log(`rendering ${o.rom} → ${base}${o.split === "mix" ? "" : "_*"} (${how})…`);

  // Stream PCM straight to the WAV files as it renders (bounded memory) rather than buffering the whole song.
  const sink = buildSink(ctx, o, id, platform, sr, base, log);

  // Auto-start playback, capturing the song's opening frames as the recording's head (see pressPlay). The
  // sink exists first so the button-hold render is captured in the right mix/split shape, not discarded.
  const leadIn = pressPlay(ctx, sink, o, id, platform);

  if (autoDetect) {
    const m = driveAutoDetect(sink, isPlaying!, o.maxDurationMs, sr, hooks, leadIn);
    const outputs = sink.finishAll();
    const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
    log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
    if (!m.hff) warn(`no HFF stop within ${o.maxDurationMs}ms — add an HFF to the song end for exact length`);
    hooks.onRendered?.(lengthMs); // settle on the length actually written (the held tail is trimmed off)
    return { outputs, lengthMs, frames: m.endFrame - m.startFrame, hff: m.hff };
  }
  const targetFrames = Math.floor(((o.durationMs ?? o.maxDurationMs) * sr) / 1000); // exact target frame count
  driveFixed(sink, targetFrames, sr, hooks, leadIn);
  const outputs = sink.finishAll();
  hooks.onRendered?.(Math.round((targetFrames / sr) * 1000));
  return { outputs };
}
