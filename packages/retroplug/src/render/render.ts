// The shared render orchestration: boot a fresh system from a ROM (+ its battery .sav / a savestate) and
// stream its audio to WAV - full mix or per-channel/per-pin stems, with tracker song-length auto-detection.
// Host-neutral: it drives a RenderContext (a booted control plane) and reports through RenderHooks. The render
// worker (worker.ts) calls runRenderJob inside every engine job, and the CLI `render` command (cli/sessions/render.ts)
// and the UI's System > Render both submit one; improving this file improves both.
//
// Nothing here names a console or a cart (docs/modular-graph-phase-f4a-design.md section 5.1). A ROM is classified
// through the console registry (its content first, its extension only where there is no content to read), the roles it
// will get are the registry's defaults for that console, the tracker integration those roles name supplies the song,
// play and end-detection hooks, and the console's descriptor supplies the splits, their file names and the default
// gesture. The pipeline stays this file's: the boot, the settle, the lead-in, the sink, the end detection's framing
// and the part files.
//
// Song-length auto-detect: when the ROM's integration offers a probe (LSDj's NR52, risa's sequencer flag) and no fixed
// duration is pinned, render to the song's end, report the length, and trim the output to it. A pinned duration forces
// a fixed length; maxDurationMs caps the detection.
//
// Song selection: a sav holds saved songs, and a cart that keeps its working song in the battery takes the chosen one
// as the SRAM a fresh system boots from (LSDj, risa), while one whose working song is work RAM (smsggdj) has it
// written in after the boot settle. Which is which is the integration's (`render.seedSong` / `render.loadSong`).
import { createWavWriter, type WavWriter } from "./wav";
import { basename, dirname, extensionLower, stem, joinPath, uniqueBase } from "../pathUtil";
import { siblingSavPath } from "../savPaths";
import {
  classifyRom,
  consoleOfPlatform,
  defaultCoreFor,
  platformEntry,
  platformForExtension,
  registeredPlatforms,
  type ConsoleDescriptor,
  type ConsoleSplit,
  type Core,
  type Platform,
  type PrefixReader,
} from "../consoles";
import { rolesForRom } from "../rolesForRom";
import { TRACKER_INTEGRATIONS, resolveTracker } from "../trackerRegistry";
import type { RoleInstance } from "@retroplug/core/systemRoles";
import type { Host } from "@retroplug/core/host";
import type { SongInfo } from "@retroplug/tracker/songCatalog";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import { PLAY_HOLD_MS, type RenderFacts, type RenderPlaySink, type TrackerRender } from "@retroplug/tracker/trackerRender";
import { renderHost } from "./renderHost";
import {
  type RenderContext,
  type RenderHooks,
  type RenderOpts,
  type RenderResult,
  type SplitMode,
  RenderCancelled,
} from "./types";

// A fixed (non-auto-detect) render runs for `durationMs` if pinned, else `maxDurationMs` - so "max duration"
// bounds every render, not just the auto-length cap. maxDurationMs defaults to 600000 (10 min) at the
// CLI / worker seams, so a render with neither pinned falls back to that cap.

const DETECT_OFF_CHUNKS = 2; // the probe must read off this many consecutive chunks to count as the song's stop

/** The console a render's ROM runs on, the core that runs it, the roles a fresh system of it will get, and the tracker
 *  integration those roles name. `platform` is "unknown" for a file whose content names no registered console and whose
 *  extension names none either; the render then has no hooks and no splits, and the host's own classify (the store's)
 *  reports the failure when it builds the system.
 *
 *  The roles are the registry's defaults for the console - the same list the store's build starts from - so a render
 *  asks the integration the built system would have, rather than sniffing a ROM marker itself (edge E2). */
export interface RenderRom {
  readonly platform: Platform | "unknown";
  readonly core: Core | undefined;
  readonly roles: RoleInstance[];
  readonly integration: TrackerIntegration | undefined;
  /** False when the file could not be read at all: its platform is then the extension's (edge E1) and its roles are
   *  the console's defaults with nothing the header would derive, so a song flag's refusal names the read rather than
   *  a console that runs no tracker (edge E8). */
  readonly readable: boolean;
}

/** Classify a render's ROM: its content first, and its extension only for a file there is no content to read from (edge
 *  E1 - the failure then comes from the load, as it always did, and names the file). */
function classifyForRender(
  reader: PrefixReader,
  romPath: string,
): { platform: Platform | "unknown"; readable: boolean } {
  let unreadable = false;
  const platform = classifyRom(
    {
      readFilePrefix: (path: string, length: number) => {
        const bytes = reader.readFilePrefix(path, length);
        if (bytes === null) unreadable = true;
        return bytes;
      },
    },
    romPath,
  );
  if (!unreadable) return { platform, readable: true };
  const ext = extensionLower(romPath);
  return { platform: (ext === "" ? undefined : platformForExtension(ext)) ?? platform, readable: false };
}

/** The console, core, roles and integration a render's ROM names (see RenderRom). */
export function resolveRenderRom(ctx: RenderContext, romPath: string): RenderRom {
  const { platform, readable } = classifyForRender(ctx.backend, romPath);
  if (platform === "unknown") return { platform, core: undefined, roles: [], integration: undefined, readable };
  const core = defaultCoreFor(platform);
  // The store's own build reads the roles through this same function, so the integration a render asks is the one the
  // built system will have, whatever a role derives from the file (src/rolesForRom.ts).
  const roles = core === undefined ? [] : rolesForRom(ctx.registry, ctx.backend, { core, platform, romPath });
  return { platform, core, roles, integration: resolveTracker(roles), readable };
}

/** The console's name as its descriptor spells it ("Game Boy Advance"), for text that says what a render refused. */
function platformLabel(platform: Platform | "unknown"): string {
  if (platform === "unknown") return "unknown";
  return platformEntry(platform)?.label ?? platform;
}

/** Why a song flag found no cart for a render's ROM: the console names no tracker, or, when the file could not be read,
 *  that is the cause and the console word would mislead (edge E8). */
function noTrackerReason(readable: boolean, platform: Platform | "unknown"): string {
  return readable
    ? `this ROM runs no tracker: its console is ${platformLabel(platform)}`
    : "this ROM could not be read";
}

/** The word a split refusal uses for a ROM's console: the platform id, and `other` for a file nothing classifies (the
 *  name the render's own union gave it before the registry, which the refusal text still prints). */
function refusalWord(platform: Platform | "unknown"): string {
  return platform === "unknown" ? "other" : platform;
}

/** The consoles that can write `split`, as their platform labels ("Game Boy", "NES"), in product order: what a
 *  --split refusal says the flag needs. */
function splitPlatformLabels(split: "channels" | "pins"): string[] {
  return registeredPlatforms()
    .filter((platform) => consoleOfPlatform(platform.id)?.splits?.[split] !== undefined)
    .map((platform) => platform.label);
}

/** The integrations whose carts can play a saved song, by label, in product order: what a --song refusal names. */
function songCartLabels(): string {
  const labels = TRACKER_INTEGRATIONS.filter(
    (t) => t.render?.seedSong !== undefined || t.render?.loadSong !== undefined,
  ).map((t) => t.label);
  return labels.length ? labels.join(", ") : "no cart this build carries";
}

/** A battery's saved songs as "--list-songs" and a not-found message spell them, unless the cart has a spelling of its
 *  own (its `songList`). */
export function songList(songs: readonly SongInfo[]): string {
  return songs.length ? songs.map((s) => `${s.index}: ${s.name || "(unnamed)"}`).join(", ") : "(no saved songs)";
}

/** The one-line listing a song-selection error prints for this cart. */
function listingOf(render: TrackerRender | undefined, songs: readonly SongInfo[]): string {
  return render?.songList?.listing?.(songs) ?? songList(songs);
}

/** The battery a render's song flags read: `--sav` when given, else the ROM's sibling .sav. */
function readBattery(ctx: RenderContext, o: RenderOpts): { path: string; raw: Uint8Array } {
  const path = o.sav ?? siblingSavPath(o.rom);
  const raw = ctx.backend.readFile(path);
  if (!raw) throw new Error(`render: no sav to read songs from at ${path}`);
  return { path, raw };
}

/** The saved songs a --list-songs asks for: the battery next to the ROM (or `--sav`), read through the ROM's
 *  integration. Throws when the ROM's cart has no song list, naming the carts that do; `empty` is what the caller
 *  prints under its header when the battery holds none. */
export function renderSongs(
  ctx: RenderContext,
  o: RenderOpts,
): { path: string; songs: readonly SongInfo[]; empty: string } {
  const resolved = resolveRenderRom(ctx, o.rom);
  const render = resolved.integration?.render;
  if (!render?.listSongs) {
    throw new Error(
      `render: --list-songs is a feature of ${songCartLabels()} ` +
        `(${noTrackerReason(resolved.readable, resolved.platform)})`,
    );
  }
  const { path, raw } = readBattery(ctx, o);
  return { path, songs: render.listSongs(raw), empty: render.songList?.empty ?? "(no saved songs)" };
}

/** The slot a song flag names, resolved through the integration's own list: by index, else by name (case-insensitive; a
 *  name several slots share takes the first and says so). A cart whose names have a fixed length matches the requested
 *  name truncated to it, as LSDj's always did. Throws naming the songs a miss could have meant. */
function chooseSong(
  o: RenderOpts,
  render: TrackerRender,
  songs: readonly SongInfo[],
  warn: (msg: string) => void,
): SongInfo {
  let hit: SongInfo | undefined;
  if (o.songIndex !== undefined) {
    hit = songs.find((s) => s.index === o.songIndex);
    if (!hit) throw new Error(`render: slot ${o.songIndex} is empty; songs: ${listingOf(render, songs)}`);
  } else {
    const wanted = o.song!.trim().toUpperCase();
    const want = render.songNameLength === undefined ? wanted : wanted.slice(0, render.songNameLength);
    const hits = songs.filter((s) => s.name.trim().toUpperCase() === want);
    if (hits.length === 0) throw new Error(`render: no song named "${o.song}"; songs: ${listingOf(render, songs)}`);
    if (hits.length > 1) warn(`render: ${hits.length} songs named "${o.song}"; using slot ${hits[0].index}`);
    hit = hits[0];
  }
  return hit;
}

/** The role list to build the render's core from: the instance's own roles when the caller carried them (the UI's
 *  System > Render), else none - which lets adopt/addSystem attach defaults, as a CLI render wants. A split's
 *  `configPatch` is folded INTO the carried core-config role (the role named after the core) rather than replacing the
 *  list, so `--split channels` and a project's region / expansion volume / accuracy switches all survive together. The
 *  render's own split decides the patched field outright: it is the one knob the request owns, and a stale value pinned
 *  in the project must not arm capture the request did not ask for. */
function rolesForRender(
  carried: RoleInstance[] | undefined,
  core: Core | undefined,
  patch: Readonly<Record<string, unknown>> | undefined,
): RoleInstance[] | undefined {
  if (core === undefined || patch === undefined) return carried?.length ? carried : undefined;
  if (!carried?.length) return [{ kind: core, config: { ...patch } }];
  const merged = carried.map((r) => (r.kind === core ? { ...r, config: { ...r.config, ...patch } } : r));
  return merged.some((r) => r.kind === core) ? merged : [...merged, { kind: core, config: { ...patch } }];
}

/** The split the request asks for on this console, if it has one: its names, its config patch and its tap mode. */
function splitOf(descriptor: ConsoleDescriptor | undefined, split: SplitMode): ConsoleSplit | undefined {
  return split === "mix" ? undefined : descriptor?.splits?.[split];
}

/** Build the single system; the graph it publishes places its protocol nodes, and its system node the carried gain
 *  and core config's knobs. `seed` (the chosen song's SRAM) forces the adopt path; so do carried roles / gain (adopt
 *  takes them as a load takes a stored system: the roles' construct-time fields reach the build) and a split's config
 *  patch arming construct-time capture; otherwise addSystem auto-detects. */
function buildSystem(
  ctx: RenderContext,
  o: RenderOpts,
  core: Core | undefined,
  patch: Readonly<Record<string, unknown>> | undefined,
  seed?: Uint8Array,
): number {
  let id: number | null;
  const roles = rolesForRender(o.roles, core, patch);
  if (seed || roles || o.gainDb) {
    // A chosen song's SRAM (adopt takes raw SRAM bytes), the instance's carried configuration, and a split's
    // construct-time capture all go through adopt - combined here so `--song-index ... --split channels` arms both. A
    // seed replaces the sav; without one, pair the sav.
    const spec: Parameters<typeof ctx.project.systems.adopt>[0] = { romPath: o.rom };
    if (!seed && o.sav) spec.savPath = o.sav;
    if (roles) spec.roles = roles;
    if (o.gainDb) spec.settings = { gainDb: o.gainDb };
    ctx.project.systems.adopt(spec, seed ? { sramBytes: seed } : undefined);
    id = ctx.project.systems.view()[0]?.id ?? null;
  } else {
    // A mix or a split with no patch: addSystem auto-detects roles. Sibling <rom>.sav auto-pairs when sav absent.
    id = ctx.project.systems.addSystem(o.rom, { explicitSav: o.sav });
  }
  if (id == null) throw new Error(`render: could not load ROM: ${o.rom}`);
  if (o.state && ctx.project.systems.loadState(id, o.state) == null)
    throw new Error(`render: could not load savestate: ${o.state}`);
  return id;
}

/** The working song's name for a tracker cart, read straight from the battery - defaults the output filename when
 *  neither --out nor a --song selection is given. Null for a non-tracker ROM / no sav (the ROM name then). The catalog
 *  is the ROM's integration's, which is the same rule the end detection follows (edge E2): a non-LSDj Game Boy next to
 *  an LSDj battery is named after its ROM, not the battery's song. */
function currentSongName(
  ctx: RenderContext,
  o: RenderOpts,
  integration: TrackerIntegration | undefined,
): string | null {
  const raw = ctx.backend.readFile(o.sav ?? siblingSavPath(o.rom));
  if (!raw) return null;
  return integration?.songs?.workingName(raw) ?? null;
}

/** The file a render streams `path` to before it is moved over `path`: `<path>.<token>.part`, the token the render's
 *  own (partToken), so two renders of one target never share one and a render never opens, truncates or deletes a
 *  `.part` another left. */
const partPath = (path: string, token: string): string => `${path}.${token}.part`;

/** Whether the directory entry `name` is a `.part` of the file named `file`, any render's; `folds` compares them as a
 *  filesystem that matches names without regard to case does (foldsCase). */
const isPartOf = (name: string, file: string, folds: boolean): boolean => {
  const [n, f] = folds ? [name.toLowerCase(), file.toLowerCase()] : [name, file];
  return n.startsWith(`${f}.`) && n.endsWith(".part");
};

/** Whether the filesystem holding `dir` matches names without regard to case (Windows, a default macOS volume), as
 *  fileExists then does: asked for one of `names` (the directory's entries) with the case of each letter swapped, such
 *  a filesystem answers it and a case-sensitive one does not, unless the directory also holds that spelling, which its
 *  listing then shows. A directory with no entry to ask about holds no `.part` to match either. */
function foldsCase(ctx: RenderContext, dir: string, names: string[]): boolean {
  const plain = names.map((n) => n.replace(/\/$/, ""));
  const probe = plain.find((n) => n.toLowerCase() !== n.toUpperCase());
  if (probe === undefined) return false;
  const swapped = [...probe].map((c) => (c === c.toUpperCase() ? c.toLowerCase() : c.toUpperCase())).join("");
  return !plain.includes(swapped) && ctx.backend.fileExists(joinPath(dir, swapped));
}

let partTokens = 0; // the tokens this runtime drew: one runtime can run several renders (the in-process tests)

/** A part token no other render's shares: the clock (ms), the runtime's Math.random (seeded from the microsecond clock
 *  when the runtime was created), the render's `tag` when its host gives one, and a count of this runtime's tokens. An
 *  engine job's tag is its RenderHost's serial, which no other render in the process has, so two renders in one
 *  process never tie, whatever the clock and the draw; the count separates the renders one runtime runs (the
 *  in-process tests, which have no tag). Renders in two processes would need the same millisecond and the same draw,
 *  and a token is drawn again while any of its part names for `outputs` is on disk. */
function partToken(ctx: RenderContext, outputs: string[], tag: string | undefined): string {
  for (;;) {
    const parts = [Date.now().toString(36), Math.floor(Math.random() * 2 ** 32).toString(36)];
    if (tag !== undefined) parts.push(tag);
    const token = [...parts, partTokens++].join("-");
    if (!outputs.some((p) => ctx.backend.fileExists(partPath(p, token)))) return token;
  }
}

// A safe filename fragment for a song-derived output name (mirrors the CLI sanitize in lsdj-rom/risa-rom).
const sanitizeRenderName = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_");

/** The WAV output base. With --out: the given path (mix) or a prefix (split, a trailing .wav trimmed).
 *  Without it: the working/selected SONG name for a tracker cart else the ROM stem, written next
 *  to the ROM - so `songName === null` is byte-identical to the old ROM-derived default. Exported for tests. */
export function outBase(o: RenderOpts, songName: string | null): string {
  if (o.out) return o.split === "mix" ? o.out : o.out.toLowerCase().endsWith(".wav") ? o.out.slice(0, -4) : o.out;
  const clean = songName ? sanitizeRenderName(songName) : "";
  const base = joinPath(dirname(o.rom), clean || stem(o.rom));
  return o.split === "mix" ? `${base}.wav` : base;
}

/** The WAV file paths a render will write for `prefix` (a base WITHOUT the extension), mirroring buildSink's
 *  naming: mix -> one `<prefix>.wav`; split -> one `<prefix>_<stream>.wav` per stream, in the split's own order. Used
 *  by the "rename" policy to detect a collision on any of the mode's outputs. */
function renderOutputPaths(o: RenderOpts, names: readonly string[], prefix: string): string[] {
  if (o.split === "mix") return [`${prefix}.wav`];
  return names.map((n) => `${prefix}_${n}.wav`);
}

/** Apply the "If Exists" policy to a resolved base, and draw the render's part token. "overwrite" (default)
 *  clobbers; "rename" bumps to the first `<name>_N` whose outputs don't yet exist (checking every file the mode would
 *  write). A `.part` of an output, any render's, counts as taken, since a render's file appears only when it ends, and
 *  the chosen name is reserved on the spot (the render's own empty `.part` per output, which the sink's writers then
 *  truncate and fill) so a render started while this one runs picks the next name instead of writing the same file.
 *  The `.part` files are found by listing the outputs' directory, as their tokens are the other renders' own, and are
 *  matched without regard to case where the filesystem matches names so (foldsCase), as fileExists then does. The
 *  reservation is made where the name is chosen; two renders choosing in the same instant could still tie, as the
 *  backend has no exclusive create. A `.part` left by a process that died mid-render (a second Ctrl-C, a crashed DAW)
 *  is never removed by a later render, which cannot tell it from a live one's: it makes every later "rename" render of
 *  that name take the next one, with no `<name>.wav` there, until it is deleted by hand. */
function resolveOnExists(
  ctx: RenderContext,
  o: RenderOpts,
  names: readonly string[],
  base: string,
  tag: string | undefined,
): { base: string; token: string; reserved: string[] } {
  if (o.onExists !== "rename") {
    const outputs = o.split === "mix" ? [base] : renderOutputPaths(o, names, base);
    return { base, token: partToken(ctx, outputs, tag), reserved: [] };
  }
  const isMix = o.split === "mix";
  const prefix = isMix && base.toLowerCase().endsWith(".wav") ? base.slice(0, -4) : base;
  // A directory's entries, and whether its filesystem folds case, worked out once per resolve.
  const listed = new Map<string, { names: string[]; folds: boolean }>();
  const entries = (dir: string): { names: string[]; folds: boolean } => {
    let e = listed.get(dir);
    if (!e) {
      const names = ctx.backend.listDir(dir || ".");
      listed.set(dir, (e = { names, folds: foldsCase(ctx, dir || ".", names) }));
    }
    return e;
  };
  const taken = (f: string) => {
    if (ctx.backend.fileExists(f)) return true;
    const { names, folds } = entries(dirname(f));
    return names.some((n) => isPartOf(n, basename(f), folds));
  };
  const unique = uniqueBase(prefix, (p) => renderOutputPaths(o, names, p).some(taken));
  const outputs = renderOutputPaths(o, names, unique);
  const token = partToken(ctx, outputs, tag);
  const reserved = outputs.map((p) => partPath(p, token));
  for (const r of reserved) {
    if (!ctx.backend.writeFile(r, new Uint8Array(0))) {
      reserved.forEach((p) => ctx.backend.deleteFile(p));
      throw new Error(`render: could not create ${r}`);
    }
  }
  return { base: isMix ? `${unique}.wav` : unique, token, reserved };
}

// --- streaming render: pump 100 ms chunks straight into per-output WavWriters (no whole-song buffer) ---

interface StopMarkers {
  startFrame: number; // frame where playback began (NR52 first on)
  endFrame: number; // frames committed to disk (== the HFF stop, or everything at the cap)
  hff: boolean; // an HFF stop was detected (vs. hitting maxMs)
}

/** A render target: how to pull the next chunk, where each chunk's frames go (one interleaved buffer per
 *  stream - mix=1, split=N), and how to close the files. Writers are created lazily on the first chunk. */
export interface RenderSink {
  renderChunk(ms: number): Float32Array[];
  emit(chunk: Float32Array[], takeFrames: number): void; // commit the first `takeFrames` frames of a chunk
  finishAll(): string[]; // patch every writer's header, move each file to its path, log + return the output paths
  discard(): void; // delete the files opened so far: a render stopped part way leaves no WAV claiming it is empty
}

/** Left channel of an interleaved-stereo buffer, first `frames` frames (a mono console's streams are in the L lane). */
function leftLane(b: Float32Array, frames: number): Float32Array {
  const out = new Float32Array(frames);
  for (let f = 0; f < frames; f++) out[f] = b[f * 2];
  return out;
}

/** Build the sink for the requested output shape. Writers open on the first chunk once the stream count is
 *  known (from chunk.length). Each writer streams to `<path>.<token>.part` (partPath) and finishAll moves it over
 *  `<path>`, so a render that stops part way (cancelled, or failed) leaves an export that was already at `<path>` as
 *  it was, and any other render's `.part` of it too.
 *    mix       -> 1 WAV: as many lanes as the console's mix has (a stereo console's two, a mono console's left one)
 *    channels  -> one stem per stream of the split, in the split's order and lanes
 *    pins      -> one stem per pin stream
 *  (No combined multi-channel WAV - split modes write per-stream files only.) */
function buildSink(
  ctx: RenderContext,
  o: RenderOpts,
  id: number,
  platform: Platform | "unknown",
  names: readonly string[],
  mono: boolean,
  sr: number,
  base: string,
  token: string,
  reserved: string[],
  log: (msg: string) => void,
): RenderSink {
  const renderChunk = o.split === "mix"
    ? (ms: number) => [ctx.audio.renderAudio(ms)]
    : (ms: number) => ctx.audio.renderAudioPerChannel(id, ms);

  let writers: WavWriter[] | null = null; // per stream
  const paths: string[] = [];
  const partial = (path: string) => partPath(path, token);
  let finished = false; // every file is whole: what is left as a .part from here on is a render, not a partial
  let summary = "";

  const open = (path: string, channels: number): WavWriter => {
    paths.push(path);
    return createWavWriter(ctx.backend, partial(path), sr, channels);
  };

  const ensure = (streamCount: number) => {
    if (writers) return;
    const lanes = mono ? 1 : 2;
    if (o.split === "mix") {
      writers = [open(base, lanes)];
      summary = `rendered ${o.rom} → ${base} (@${sr}Hz)`;
    } else {
      writers = Array.from({ length: streamCount }, (_, i) => open(`${base}_${names[i] ?? `ch${i}`}.wav`, lanes));
      // The console's short name is its platform id as the summaries always spelled it ("GB", "NES"); which form a
      // split takes is its lanes', so a stereo split is the console's channel render and a mono one takes its mode's.
      const consoleName = platform.toUpperCase();
      summary = mono
        ? `${consoleName} ${o.split} (${streamCount} mono streams @${sr}Hz) → ${base}_*`
        : `${consoleName} ${streamCount}-channel render (@${sr}Hz) → ${base}_*`;
    }
  };

  return {
    renderChunk,
    emit(chunk, take) {
      ensure(chunk.length);
      if (o.split === "mix") {
        writers![0].append(mono ? leftLane(chunk[0], take) : chunk[0].subarray(0, take * 2));
      } else {
        chunk.forEach((b, i) => writers![i].append(mono ? leftLane(b, take) : b.subarray(0, take * 2)));
      }
    },
    finishAll() {
      if (!writers) throw new Error("render: no audio was rendered");
      writers.forEach((w) => w.finish());
      finished = true;
      // Each .part is synced to the disk before it is moved over its path: a move replaces the name at once, and a
      // filesystem need not have written the data by then, so a power loss just after an unsynced move could leave a
      // short file where a whole export was. One that cannot be synced is not moved, as one that cannot be moved.
      const moved: string[] = [];
      for (const p of paths) {
        if (ctx.backend.syncFile(partial(p)) && ctx.backend.rename(partial(p), p)) { moved.push(p); continue; }
        // Every .part is a whole render by now, possibly minutes of work and the only copy: none is deleted, and the
        // error says which were moved and which are left.
        const left = paths.filter((q) => q !== p && !moved.includes(q)).map(partial);
        const leftAs = left.length > 1 ? "are left as .part files" : "is left as a .part file";
        throw new Error(
          `render: could not replace ${p}; the render is at ${partial(p)}` +
            (moved.length ? `; ${moved.join(", ")} ${moved.length > 1 ? "were" : "was"} moved into place` : "") +
            (left.length ? `, and ${left.join(", ")} ${leftAs}` : ""),
        );
      }
      paths.forEach((p) => log(p));
      log(summary);
      return paths.slice();
    },
    discard() {
      // A writer patches its header only in finish(), so a render stopped part way would leave PCM behind a header
      // that claims none. A stopped export is not an export: remove what it opened (and the names it reserved) rather
      // than leave a file that looks usable (the job's status says cancelled or error, and a shell sees a non-zero
      // exit). The files are this render's own `.part` ones, so what was at the output paths before stays, and so does
      // another render's `.part` of them. Once the render is whole (finishAll got that far) nothing is removed: a file
      // already moved into place is an export, and a `.part` whose move failed is the finished render.
      if (finished) return;
      for (const p of paths) ctx.backend.deleteFile(partial(p));
      for (const r of reserved) ctx.backend.deleteFile(r);
    },
  };
}

/** Fixed-duration render: stream exactly `targetFrames` frames (trim the last chunk). On the continuous-blocks engine
 *  of an engine job the output is byte-identical to a single renderAudio(ms) of the same length for any sample rate /
 *  ms / pull; on an exact-blocks engine (the in-process tests) only while no node consumes the transport or MIDI
 *  block by block (a sync node such as rp.sms.sync), whose clock then follows the pulls' splits. `leadIn` is the
 *  captured play-gesture audio (the song's opening frames) - committed first so the recording's head isn't
 *  lost, trimmed against the target like any other chunk. Exported for tests. */
export function driveFixed(
  sink: RenderSink, targetFrames: number, sr: number, hooks: RenderHooks, pullMs: number, leadIn: Float32Array[][] = [],
): void {
  let done = 0;
  const emit = (chunk: Float32Array[]) => {
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    if (chunk[0].length === 0) throw new Error("render: chunk render returned no frames");
    const take = Math.min(chunk[0].length / 2, targetFrames - done);
    if (take > 0) { sink.emit(chunk, take); done += take; }
  };
  for (const chunk of leadIn) { if (done >= targetFrames) break; emit(chunk); }
  while (done < targetFrames) {
    if (hooks.isCancelled?.()) throw new RenderCancelled();
    emit(sink.renderChunk(pullMs));
    hooks.onRendered?.((done / sr) * 1000); // committed audio so far (the last chunk is trimmed to the target)
  }
}

/** Length auto-detect render: stream chunk-by-chunk, polling `isPlaying` (LSDj's NR52 / risa's seq_mode),
 *  and stop at the HFF (the probe going true->false, sustained >= DETECT_OFF_CHUNKS). Holds back the current
 *  contiguous off-streak (<= DETECT_OFF_CHUNKS whole chunks) so committed frames end exactly at the stop; a
 *  reset flushes them in order. Caps at maxMs (no-HFF fallback -> keep everything). Each chunk is `pullMs` long,
 *  so the poll, and with it where the render can end, follows it. */
function driveAutoDetect(
  sink: RenderSink, isPlaying: () => boolean, maxMs: number, sr: number, hooks: RenderHooks, pullMs: number,
  leadIn: Float32Array[][] = [],
): StopMarkers {
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
  // (the HFF tail-detection below still ends the song at the right place). startFrame stays 0 - the whole
  // committed output is song, so the reported length includes this head.
  for (const chunk of leadIn) {
    commit(chunk);
    total += chunk[0].length / 2;
    elapsed += PLAY_HOLD_MS;
    armed = true;
  }

  while (elapsed < maxMs) {
    if (hooks.isCancelled?.()) throw new RenderCancelled();
    const chunk = sink.renderChunk(pullMs);
    if (chunk.length === 0) throw new Error("render: chunk render returned no streams");
    const frameBefore = total;
    total += chunk[0].length / 2;
    elapsed += pullMs;
    // Rendered audio so far, counting the held off-streak: monotonic, and at most DETECT_OFF_CHUNKS ahead
    // of what lands on disk (a confirmed HFF stop discards those held chunks).
    hooks.onRendered?.((total / sr) * 1000);

    const on = isPlaying();
    if (!armed) {
      commit(chunk); // keep the lead-in from frame 0
      if (on) { armed = true; startFrame = frameBefore; } // playback began here
      continue; // (still un-armed just means playback hasn't started yet - keep going to the cap)
    }
    if (!on) {
      held.push(chunk); // hold back - this may be the HFF tail
      if (held.length >= DETECT_OFF_CHUNKS) // sustained -> HFF stop; discard held, end at the first off frame
        return { startFrame, endFrame: committed, hff: true };
    } else {
      for (const c of held) commit(c); // song continues: flush the (kept) off-streak in order...
      held.length = 0;
      commit(chunk); // ...then this chunk
    }
  }
  for (const c of held) commit(c); // cap reached, no confirmed stop -> keep everything
  return { startFrame, endFrame: committed, hff: false };
}

/** Validate a resolved render request against the console it runs on. Throws on a bad split or a song flag no cart of
 *  the console can honour, so the caller fails before loading anything. (Shared by the CLI and the worker.) */
export function validateRenderOpts(
  o: RenderOpts,
  platform: Platform | "unknown",
  render?: TrackerRender | undefined,
  readable = true,
): void {
  // Song selection promotes or loads a saved song, which only a cart with a render hook for it can do: LSDj, risa and
  // smsggdj do, BlipToaster (no song battery) does not, and neither does a console the build runs no tracker on.
  const canPlaySaved = render?.seedSong !== undefined || render?.loadSong !== undefined;
  if ((o.song !== undefined || o.songIndex !== undefined) && !canPlaySaved) {
    const carts = songCartLabels();
    // The reason is the ROM's ROLES, not its console: a Game Boy ROM whose title names no tracker (mGB, a homebrew
    // cart) runs none, though the console has carts that do, so the sentence says what this ROM lacks; a file that
    // could not be read has no roles at all, and says so rather than blaming its console (edge E8).
    throw new Error(
      `render: --song/--song-index is a feature of ${carts} (${noTrackerReason(readable, platform)})`,
    );
  }
  // A split is the console descriptor's: which output layouts its core can be built in, and the file each stream takes.
  const splits = platform === "unknown" ? undefined : consoleOfPlatform(platform)?.splits;
  if (o.split === "pins" && splits?.pins === undefined) {
    throw new Error(
      `render: --split pins is ${splitPlatformLabels("pins").join(" or ")}-only (got ${refusalWord(platform)})`,
    );
  }
  if (o.split === "channels" && splits?.channels === undefined) {
    throw new Error(
      `render: --split channels needs a ${splitPlatformLabels("channels").join(" or ")} ROM ` +
        `(got ${refusalWord(platform)})`,
    );
  }
}

/** Auto-start playback and CAPTURE the song's opening frames through the sink. The song begins the moment
 *  Start is pressed, so the button-hold render (needed to register the press) must be captured as the
 *  recording's head - not discarded, which drops the first ~100 ms. The gesture is the cart's own where its integration
 *  has one (risa's SELECT+START with its prep presses, smsggdj's Pause and its already-playing check), else the console
 *  descriptor's default (a Game Boy's Start, which LSDj and mGB begin on), else none (a generic NES or SMS ROM is left
 *  untouched). Returns the captured lead-in chunks (in the sink's mix/split shape). */
function pressPlay(
  host: Host<"emulator">,
  sink: RenderPlaySink,
  o: RenderOpts,
  id: number,
  rom: Uint8Array | undefined,
  descriptor: ConsoleDescriptor | undefined,
  render: TrackerRender | undefined,
): Float32Array[][] {
  if (!o.start) return [];
  if (render?.pressPlay) return render.pressPlay(host, id, rom ?? new Uint8Array(), sink);
  const button = descriptor?.render?.defaultPlay?.button;
  if (button === undefined) return [];
  host.emulator.pressButton(id, button, true);
  const head = sink.renderChunk(PLAY_HOLD_MS); // hold it across one captured chunk to register the press
  host.emulator.pressButton(id, button, false); // a play toggle: the song keeps running once the release lands next block
  return [head];
}

/** Boot a fresh system from the request and stream its audio to WAV. Returns the written paths (+ the
 *  detected length for an auto-detect render). Host-neutral - the CLI and the render worker both call
 *  this; hooks default to console logging with no progress/cancel. Does NOT handle --list-songs (a query,
 *  not a render - the CLI wrapper handles that through renderSongs). */
export function runRenderJob(ctx: RenderContext, o: RenderOpts, hooks: RenderHooks = {}): RenderResult {
  const log = hooks.log ?? ((m: string) => console.log(m));
  const warn = hooks.warn ?? ((m: string) => console.warn(m));
  const { platform, core, integration, readable } = resolveRenderRom(ctx, o.rom);
  const render = integration?.render;
  const descriptor = platform === "unknown" ? undefined : consoleOfPlatform(platform);
  const split = splitOf(descriptor, o.split);
  // A mono console's streams are in the left lane (a NES's), a stereo one's are interleaved (a Game Boy's); a console
  // that declares neither shape, or no render at all, is treated as stereo, which is what its mix is.
  const mono = (descriptor?.render?.mixLanes ?? "stereo") === "left";

  validateRenderOpts(o, platform, render, readable);
  const pullMs = o.pullMs ?? PLAY_HOLD_MS;
  if (!(Number.isFinite(pullMs) && pullMs > 0))
    throw new Error(`render: pullMs must be a positive number of ms (got ${o.pullMs})`);

  // The sample rate is baked into each core at construct, so it MUST be set before buildSystem.
  if (o.sampleRate !== undefined && !ctx.audio.setSampleRate(o.sampleRate))
    throw new Error(`render: could not set sample rate to ${o.sampleRate}Hz`);
  // A pull under one frame renders no audio at all, so a fixed render would never reach its length.
  const rate = ctx.audio.sampleRate();
  if (Math.floor((pullMs * rate) / 1000) < 1) {
    throw new Error(`render: pullMs ${pullMs} is under one frame at ${rate}Hz`);
  }

  // The boot (the song's promotion, the system's build, the settle, a cart's song load) is checked for a cancel
  // between its steps, as the stream is between its chunks: a cancel taken while it boots ends the render at its next
  // step rather than after its Start press, and before it has reserved or opened a file, so there is nothing to
  // discard.
  const cancelPoint = () => {
    if (hooks.isCancelled?.()) throw new RenderCancelled();
  };
  cancelPoint();
  // The ROM's whole bytes are read on first use, not here: a build hands the ROM to a core as a path (the store's own
  // read) and only a cart's hooks take the bytes into this process, so a render of a ROM with no tracker (a plain mGB
  // boot, a GBA that can never have hooks) does not pull the whole file across for nothing. A hook that reads them
  // twice gets the one read.
  let romBytes: Uint8Array | null | undefined;
  const readRom = (): Uint8Array | undefined => {
    if (romBytes === undefined) romBytes = ctx.backend.readFile(o.rom);
    return romBytes ?? undefined;
  };
  // A chosen song is resolved BEFORE the boot, so a bad --song fails fast: one cart seeds the SRAM the fresh system
  // boots from, the other has it written in after the settle (below).
  const songFlags = o.song !== undefined || o.songIndex !== undefined;
  const battery = songFlags ? readBattery(ctx, o) : undefined;
  const chosen = battery && render?.listSongs ? chooseSong(o, render, render.listSongs(battery.raw), warn) : undefined;
  // One cart promotes the chosen song into the SRAM the fresh system boots from (a seed), the other has it written in
  // after the settle (a load); which ran is worth saying, because it is where the song ends up that decides what the
  // cart does with it. A seed the cart cannot make refuses the song here, before anything boots: playing the battery's
  // own working song under the chosen song's name would be a successful-looking render of the wrong music.
  let seed: Uint8Array | undefined;
  if (chosen && render?.seedSong) {
    const promoted = render.seedSong(battery!.raw, chosen.index);
    if (promoted === null) {
      throw new Error(
        render.seedRefusal?.(chosen.index) ??
          `render: could not seed song ${chosen.index} (${integration!.label}) from ${battery!.path}`,
      );
    }
    seed = promoted;
  }
  if (chosen) {
    // Where the song went: a seed is promoted into the battery's working song, a load writes into work RAM.
    const destination = render?.seedSong ? "working song" : "work RAM";
    log(`song "${chosen.name || "(unnamed)"}" (slot ${chosen.index}) → ${destination}`);
  }
  cancelPoint();
  const id = buildSystem(ctx, o, core, split?.configPatch, seed);
  cancelPoint();

  // Settle boot (past the mGB/LSDj splash) before driving playback. Discarded, but it runs in the shape the render
  // streams in: on a continuous-blocks engine (an engine job's) the frames past a call in its last block go to the
  // next call only if that call is of the same shape (EngineRpcService::setContinuousBlocks), so a split render
  // settling in the mix would start its stems up to a block later than the same render's mix starts, and its detected
  // length would differ by as much. An exact-blocks engine carries nothing; there the shape still moves a NES's stem
  // bytes (a settle per channel emulates it differently from one in the mix), and no in-process caller renders split.
  if (o.split === "mix") ctx.audio.renderAudio(1500);
  else ctx.audio.renderAudioPerChannel(id, 1500);
  cancelPoint();
  if (o.bpm) ctx.audio.setBpm(o.bpm);
  if (o.transport) ctx.audio.setTransport(true);

  // What this cart's render owes the user a word about (a boot that will be silent), then AFTER the settle the cart's
  // own song load: a cart whose working song is work RAM blanks it on the way up, so an earlier write would be wiped.
  const facts: RenderFacts = {
    // A getter, so a cart's warnings are what reads the ROM: the boot already handed it to the core as a path.
    get rom() {
      return readRom() ?? null;
    },
    sav: battery?.raw ?? ctx.backend.readFile(o.sav ?? siblingSavPath(o.rom)),
    state: o.state !== undefined,
    song: chosen !== undefined,
    durationMs: o.durationMs,
    maxDurationMs: o.maxDurationMs,
  };
  render?.warnings?.(facts, warn);
  if (chosen && render?.loadSong) {
    // The build just read the ROM, but a load that cannot is the same failure the cart's own reader reported, and a
    // silent skip would leave the boot song rendering under the chosen song's name.
    const rom = readRom();
    if (!rom || !battery) {
      throw new Error(`render: could not re-read the ROM/sav to load ${integration!.id} song ${chosen.index}`);
    }
    render.loadSong(renderHost(ctx), id, rom, battery.raw, chosen.index);
  }
  cancelPoint();

  const sr = ctx.audio.sampleRate();
  // Default the filename to the SONG: the selected song when --song/--song-index is used, else the battery's
  // working song. --out overrides both, so skip the battery read then.
  const songName = o.out ? null : chosen ? chosen.name : currentSongName(ctx, o, integration);
  const names = split?.names ?? [];
  const { base, token, reserved } = resolveOnExists(ctx, o, names, outBase(o, songName), hooks.partTag);

  // Stream PCM straight to the WAV files as it renders (bounded memory) rather than buffering the whole song.
  const sink = buildSink(ctx, o, id, platform, names, mono, sr, base, token, reserved, log);

  // Auto-start playback, capturing the song's opening frames as the recording's head (see pressPlay). The
  // sink exists first so the button-hold render is captured in the right mix/split shape, not discarded.
  try {
    const host = renderHost(ctx);
    // Song-length auto-detect: when the ROM's cart offers a probe (LSDj on a Game Boy, risa on a NES) and the user
    // didn't pin a duration, render to the song's end instead of a fixed window, report the length, and trim the
    // silent tail. A cart with no probe (smsggdj, a generic ROM) renders a fixed length, and says why where the probe
    // answers null.
    const isPlaying = render?.playingProbe
      ? render.playingProbe(host, id, readRom() ?? new Uint8Array(), facts, warn)
      : null;
    // Auto-detect applies to both mix and split - split just renders per-channel chunks.
    const autoDetect = isPlaying !== null && o.start && o.durationMs === undefined;

    // Announce before the (possibly multi-minute) render so callers don't look hung while it works.
    const how = autoDetect
      ? `detecting length (HFF, cap ${o.maxDurationMs}ms)`
      : `${o.durationMs ?? o.maxDurationMs}ms`;
    log(`rendering ${o.rom} → ${base}${o.split === "mix" ? "" : "_*"} (${how})…`);

    const leadIn = pressPlay(host, sink, o, id, render?.pressPlay ? readRom() : undefined, descriptor, render);

    if (autoDetect) {
      const m = driveAutoDetect(sink, isPlaying!, o.maxDurationMs, sr, hooks, pullMs, leadIn);
      const outputs = sink.finishAll();
      const lengthMs = Math.round(((m.endFrame - m.startFrame) / sr) * 1000);
      log(`length: ${lengthMs} ms (${m.endFrame - m.startFrame} frames @${sr}Hz) hff:${m.hff}`);
      if (!m.hff) warn(`no HFF stop within ${o.maxDurationMs}ms - add an HFF to the song end for exact length`);
      hooks.onRendered?.(lengthMs); // settle on the length actually written (the held tail is trimmed off)
      return { outputs, lengthMs, frames: m.endFrame - m.startFrame, hff: m.hff };
    }
    const targetFrames = Math.floor(((o.durationMs ?? o.maxDurationMs) * sr) / 1000); // exact target frame count
    driveFixed(sink, targetFrames, sr, hooks, pullMs, leadIn);
    const outputs = sink.finishAll();
    hooks.onRendered?.(Math.round((targetFrames / sr) * 1000));
    return { outputs };
  } catch (e) {
    sink.discard();
    throw e;
  }
}
