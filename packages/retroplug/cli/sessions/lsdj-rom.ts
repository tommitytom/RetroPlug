// `retroplug-cli lsdj-rom` — inspect, extract and edit the static assets (sample kits, palettes, fonts)
// inside an LSDj `.gb` ROM, headlessly. Backed by the pure-TS src/lsdj/rom module; this is just the CLI
// surface (arg parsing + file I/O via the backend + WAV/JSON output).
//
// ONE JSON schema drives every editing verb (see LSDJ_ROM_HELP): a manifest of kit/palette/font entries,
// each of which BUILDS/IMPORTS from a file or TWEAKS metadata. `patch` reads the whole manifest; `build-kit`
// reads a single, slotless kit entry and compiles it to a `.kit` file.
//
//   retroplug-cli lsdj-rom info          <rom> [--json]
//   retroplug-cli lsdj-rom extract       <rom> <outDir> [--rate N]
//   retroplug-cli lsdj-rom patch         <rom> <manifest.json> <out>         (the whole-ROM manifest)
//   retroplug-cli lsdj-rom build-kit     <kit.json> <out.kit> [--no-rotate]  (native compile → .kit file)
//   retroplug-cli lsdj-rom import-sample <rom> <kit> <audio> [flags]         (compile one + splice)
//   retroplug-cli lsdj-rom remove-sample <rom> <kit> <slot> [--out rom]
//   retroplug-cli lsdj-rom export-palette / import-palette   (.lsdpal community palette files)
//   retroplug-cli lsdj-rom export-font    / import-font      (.png fonts; --gfx for the extended set)
//   retroplug-cli lsdj-rom export-kit     / import-kit       (.kit raw sample banks)
import type { CliTool } from "../tools";
import type { Session } from "../session";
import type { KitEffect } from "../../src/audioDriver";
import { encodeWav } from "../wav";
import { LsdjRom, buildKitBank, sampleBytesFromBank, kitSampleSpace, KIT_MAX_SAMPLE_SPACE, type KitSample } from "../../src/lsdj/rom";

// LSDj kit samples carry no intrinsic rate (pitch is set by the note played); export at a nominal
// reference rate — overridable with --rate. 11468 Hz is LSDj's middle-note playback rate.
const DEFAULT_SAMPLE_RATE = 11468;

const enc = new TextEncoder();
const sanitize = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "_";
const hex2 = (n: number): string => n.toString(16).toUpperCase().padStart(2, "0");

function openRom(s: Session, path: string): LsdjRom {
  const bytes = s.backend.readFile(path);
  if (!bytes) throw new Error(`cannot read ROM: ${path}`);
  const rom = LsdjRom.fromBytes(bytes);
  if (!rom.isLsdj) throw new Error(`not a recognised LSDj ROM (version=${rom.version?.raw ?? "?"}, size=${bytes.length})`);
  return rom;
}

// The asset inventory. `includeFontTiles` controls whether the (large) per-tile pixel arrays are emitted:
// `extract`'s rom.json keeps them (that's the extraction); `info --json` omits them (name + index only).
function romToJson(rom: LsdjRom, includeFontTiles: boolean): unknown {
  const kits = rom
    .kits()
    .filter((k) => k.valid)
    .map((k) => ({ index: k.index, bank: k.bank, name: k.name(), samples: k.toObject().samples.map((sm) => sm.name) }));
  const palettes = rom.palettes().map((p) => {
    const o = p.toObject();
    return { index: o.index, name: o.name, colorSets: o.colorSets.map((cs) => cs.colors.map((c) => `#${hex2(c.r)}${hex2(c.g)}${hex2(c.b)}`)) };
  });
  const fonts = rom.fonts().map((f) => (includeFontTiles ? { index: f.index, name: f.name, tiles: f.toObject().tiles } : { index: f.index, name: f.name }));
  return { version: rom.version?.raw ?? null, kits, palettes, fonts };
}

function info(s: Session, args: string[]): void {
  const json = args.includes("--json");
  const romPath = args.find((a) => !a.startsWith("--"));
  if (!romPath) throw new Error("usage: lsdj-rom info <rom> [--json]");
  const rom = openRom(s, romPath);
  if (json) {
    console.log(JSON.stringify(romToJson(rom, false), null, 2)); // no font tile data
    return;
  }
  console.log(`LSDj ROM: ${rom.version?.raw ?? "?"}`);
  const kits = rom.kits().filter((k) => k.valid);
  console.log(`kits: ${kits.length} populated`);
  for (const k of kits) console.log(`  [${String(k.index).padStart(2)}] bank ${k.bank}  ${k.name().padEnd(6)}  ${k.sampleCount()} samples`);
  const palettes = rom.palettes();
  console.log(`palettes: ${palettes.length}`);
  for (const p of palettes) console.log(`  [${String(p.index).padStart(2)}] ${p.name || "(unnamed)"}`);
  const fonts = rom.fonts();
  console.log(`fonts: ${fonts.length} (71 tiles each)`);
  for (const f of fonts) console.log(`  [${String(f.index).padStart(2)}] ${f.name || "(unnamed)"}`);
}

function extract(s: Session, args: string[]): void {
  const rateArg = args.indexOf("--rate");
  const rate = rateArg >= 0 ? parseInt(args[rateArg + 1], 10) : DEFAULT_SAMPLE_RATE;
  const positional = args.filter((a, i) => !a.startsWith("--") && !(rateArg >= 0 && i === rateArg + 1));
  const [romPath, outDir] = positional;
  if (!romPath || !outDir) throw new Error("usage: lsdj-rom extract <rom> <outDir> [--rate N]");
  const rom = openRom(s, romPath);

  const write = (name: string, bytes: Uint8Array) => {
    if (!s.backend.writeFile(`${outDir}/${name}`, bytes)) throw new Error(`write failed: ${outDir}/${name}`);
  };

  // Full metadata (names, palette colours, font tiles) as one JSON.
  write("rom.json", enc.encode(JSON.stringify(romToJson(rom, true), null, 2))); // full extraction incl. font tiles

  // Each kit sample → a mono WAV.
  let wavs = 0;
  for (const k of rom.kits()) {
    if (!k.valid) continue;
    const kitName = sanitize(k.name());
    const kit = k.toObject();
    kit.samples.forEach((sm, i) => {
      const fn = `kit${String(k.index).padStart(2, "0")}_${kitName}_${String(i).padStart(2, "0")}_${sanitize(sm.name)}.wav`;
      write(fn, encodeWav(sm.pcm, rate, 1));
      wavs++;
    });
  }
  console.log(`extracted ${wavs} sample WAVs (@${rate}Hz) + rom.json to ${outDir}`);
}

// --- sample import (native compileKit) ---

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};
const has = (args: string[], name: string): boolean => args.includes(name);

// Build the per-sample effect chain from CLI flags. Defaults mirror modern lsdpatch: peak-normalize +
// TPDF dither. Order = gain, filter (pre-resample), dither (applied post-resample by the native path).
function effectsFromFlags(args: string[]): KitEffect[] {
  const fx: KitEffect[] = [];
  if (!has(args, "--no-normalize") || flag(args, "--gain") != null) {
    fx.push({ type: "gain", normalize: !has(args, "--no-normalize"), gain: flag(args, "--gain") != null ? Number(flag(args, "--gain")) : 1 });
  }
  const filter = flag(args, "--filter");
  if (filter) {
    fx.push({
      type: "filter",
      frequency: flag(args, "--cutoff") != null ? Number(flag(args, "--cutoff")) : 5734,
      q: flag(args, "--q") != null ? Number(flag(args, "--q")) : 1,
      filterType: filter,
    } as KitEffect);
  }
  if (!has(args, "--no-dither")) fx.push({ type: "dither", ditherType: (flag(args, "--dither") ?? "HighPassTPDF") } as KitEffect);
  return fx;
}

// Resolve a possibly-relative sample path against a base file's directory (spec.json's location).
function resolvePath(baseFile: string, p: string): string {
  if (p.startsWith("/")) return p;
  const dir = baseFile.slice(0, baseFile.lastIndexOf("/") + 1);
  return dir + p;
}

// ONE schema for every verb. A `build` source is a bare path (name from the basename) or a full object.
type BuildSource = string | { file: string; name?: string; offset?: number; length?: number; effects?: KitEffect[] };

// A kit entry: build it from audio (`build`), import a `.kit` (`file`), and/or tweak metadata (`name`,
// `samples` = renames). `slot` places it in a ROM (present in a manifest; omitted for build-kit, which
// writes a slotless .kit file). Palette/font entries follow the same "file-or-tweak" shape. The whole
// manifest is `{ kits?, palettes?, fonts? }`; build-kit takes a single (slotless) kit entry.
interface KitEntry {
  slot?: number;
  name?: string;
  build?: BuildSource[];
  file?: string; // import a .kit bank
  samples?: { index: number; name: string }[]; // renames
}
interface PaletteEntry {
  slot: number;
  file?: string; // import a .lsdpal
  set?: number;
  color?: number;
  rgb?: [number, number, number];
}
interface FontEntry {
  slot: number;
  file?: string; // import a .png
  tile?: number;
  pixels?: number[];
}
interface Manifest {
  kits?: KitEntry[];
  palettes?: PaletteEntry[];
  fonts?: FontEntry[];
}

function sampleName(file: string): string {
  return file.slice(file.lastIndexOf("/") + 1).replace(/\.[^.]*$/, "");
}
function normalizeBuild(build: BuildSource[]): { file: string; name?: string; offset?: number; length?: number; effects?: KitEffect[] }[] {
  return build.map((b) => (typeof b === "string" ? { file: b } : b));
}

// Compile a kit bank from `build` sources, asserting a 16 KB bank and that every source landed (a bad path
// silently leaves an empty slot natively). Shared by build-kit + apply. `fallbackEffects` (the CLI effect
// flags) applies to sources that don't carry their own `effects`.
function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, rotate: boolean, fallbackEffects: KitEffect[]): Uint8Array {
  const sources = normalizeBuild(build);
  const bank = s.audio.compileKit({
    name,
    rotate,
    samples: sources.map((sm) => ({ path: resolvePath(baseFile, sm.file), name: sm.name ?? sampleName(sm.file), offset: sm.offset, length: sm.length, effects: sm.effects ?? fallbackEffects })),
  });
  if (bank.length !== 0x4000) throw new Error("compileKit returned an unexpected bank size");
  sources.forEach((sm, i) => {
    if (sampleBytesFromBank(bank, i).length === 0) throw new Error(`sample ${i} (${sm.file}) failed to compile — check the path`);
  });
  return bank;
}

// build-kit reads a single (slotless) kit entry — the same shape as one `kits[]` entry in a manifest — and
// compiles its `build` sources to a .kit file.
function buildKit(s: Session, args: string[]): void {
  const [specPath, outKit] = args.filter((a) => !a.startsWith("--"));
  if (!specPath || !outKit) throw new Error("usage: lsdj-rom build-kit <kit.json> <out.kit> [--no-rotate] [effect flags]");
  const spec = JSON.parse(new TextDecoder().decode(readOrThrow(s, specPath, "kit spec"))) as KitEntry;
  if (!spec.build || spec.build.length === 0) throw new Error('kit spec needs a "build" array of source audio files');

  // No target ROM here, so rotation is a build-time choice: default ON (LSDj 9.2.0+), --no-rotate for older
  // targets. Importing this .kit into a mismatched-version ROM is the straight-copy caveat (see import-kit).
  const rotate = !has(args, "--no-rotate");
  const bank = compileKitBank(s, spec.name ?? "", spec.build, specPath, rotate, effectsFromFlags(args));
  if (!s.backend.writeFile(outKit, bank)) throw new Error(`write failed: ${outKit}`);
  console.log(`built kit "${spec.name ?? ""}" (${spec.build.length} samples${rotate ? "" : ", un-rotated"}); wrote ${outKit}`);
}

function importSample(s: Session, args: string[]): void {
  const [romPath, kitStr, audio] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || kitStr == null || !audio) throw new Error("usage: lsdj-rom import-sample <rom> <kitIndex> <audio> [--name X] [--slot N] [--out rom] [effect flags]");
  const kitIndex = parseInt(kitStr, 10);
  const rom = openRom(s, romPath);
  const kit = rom.kit(kitIndex);
  const name = flag(args, "--name") ?? audio.slice(audio.lastIndexOf("/") + 1).replace(/\.[^.]*$/, "");
  const out = flag(args, "--out") ?? romPath;

  // Compile the one sample natively (a throwaway 1-sample bank) and pull its nibble bytes out. Encode to
  // match the target ROM's version so splicing into a pre-9.2.0 kit stays byte-correct.
  const oneBank = s.audio.compileKit({ name: "", rotate: rom.rotatesSamples, samples: [{ path: audio, name, effects: effectsFromFlags(args) }] });
  const bytes = sampleBytesFromBank(oneBank, 0);
  if (bytes.length === 0) throw new Error(`failed to compile ${audio} — check the path/format`);

  // Splice beside the kit's existing samples (raw, un-re-encoded).
  const samples: KitSample[] = kit.samplesRaw();
  const slotStr = flag(args, "--slot");
  const slot = slotStr === "append" || slotStr == null ? samples.length : parseInt(slotStr, 10);
  const entry = { name, bytes };
  if (slot < samples.length) samples[slot] = entry;
  else samples.push(entry);

  const space = kitSampleSpace(samples);
  if (space > KIT_MAX_SAMPLE_SPACE) throw new Error(`kit over budget: ${space} > ${KIT_MAX_SAMPLE_SPACE} bytes — remove a sample first`);
  if (samples.length > 15) throw new Error("kit already holds 15 samples — replace a slot instead of appending");

  rom.setKitBank(kitIndex, buildKitBank(kit.name(), samples));
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${name} into kit ${kitIndex} slot ${slot < samples.length ? slot : samples.length - 1}; wrote ${out}`);
}

function removeSample(s: Session, args: string[]): void {
  const [romPath, kitStr, slotStr] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || kitStr == null || slotStr == null) throw new Error("usage: lsdj-rom remove-sample <rom> <kitIndex> <slot> [--out rom]");
  const kitIndex = parseInt(kitStr, 10);
  const slot = parseInt(slotStr, 10);
  const rom = openRom(s, romPath);
  const kit = rom.kit(kitIndex);
  const samples = kit.samplesRaw();
  if (slot < 0 || slot >= samples.length) throw new Error(`slot ${slot} out of range (kit has ${samples.length} samples)`);
  samples.splice(slot, 1);
  rom.setKitBank(kitIndex, buildKitBank(kit.name(), samples));
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`removed slot ${slot} from kit ${kitIndex} (${samples.length} left); wrote ${out}`);
}

// --- asset file import/export (.lsdpal palettes, .png fonts, .kit kits) ---

function exportPalette(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || idxStr == null || !out) throw new Error("usage: lsdj-rom export-palette <rom> <index> <out.lsdpal>");
  const rom = openRom(s, romPath);
  const bytes = rom.exportPaletteFile(parseInt(idxStr, 10));
  if (bytes.length === 0) throw new Error(`palette ${idxStr} out of range`);
  if (!s.backend.writeFile(out, bytes)) throw new Error(`write failed: ${out}`);
  console.log(`wrote palette ${idxStr} to ${out}`);
}

function importPalette(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || !file || idxStr == null) throw new Error("usage: lsdj-rom import-palette <rom> <in.lsdpal> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const data = s.backend.readFile(file);
  if (!data) throw new Error(`cannot read palette: ${file}`);
  rom.importPaletteFile(parseInt(idxStr, 10), data);
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into palette ${idxStr}; wrote ${out}`);
}

function exportFont(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || idxStr == null || !out) throw new Error("usage: lsdj-rom export-font <rom> <index> <out.png> [--gfx]");
  const rom = openRom(s, romPath);
  const img = rom.exportFontImage(parseInt(idxStr, 10), has(args, "--gfx"));
  if (img.width === 0) throw new Error(`font ${idxStr} out of range / not found`);
  const png = s.backend.pngEncode(img.width, img.height, img.rgba);
  if (!png) throw new Error("PNG encode failed");
  if (!s.backend.writeFile(out, png)) throw new Error(`write failed: ${out}`);
  console.log(`wrote font ${idxStr} (${img.width}x${img.height}${has(args, "--gfx") ? ", +gfx" : ""}) to ${out}`);
}

function importFont(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || !file || idxStr == null) throw new Error("usage: lsdj-rom import-font <rom> <in.png> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const data = s.backend.readFile(file);
  if (!data) throw new Error(`cannot read font PNG: ${file}`);
  const img = s.backend.pngDecode(data);
  if (!img) throw new Error(`not a decodable PNG: ${file}`);
  rom.importFontImage(parseInt(idxStr, 10), img);
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} (${img.width}x${img.height}) into font ${idxStr}; wrote ${out}`);
}

function exportKit(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || idxStr == null || !out) throw new Error("usage: lsdj-rom export-kit <rom> <index> <out.kit>");
  const rom = openRom(s, romPath);
  const bytes = rom.exportKitFile(parseInt(idxStr, 10));
  if (bytes.length === 0) throw new Error(`kit ${idxStr} out of range`);
  if (!s.backend.writeFile(out, bytes)) throw new Error(`write failed: ${out}`);
  console.log(`wrote kit ${idxStr} bank to ${out}`);
}

function importKit(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || !file || idxStr == null) throw new Error("usage: lsdj-rom import-kit <rom> <in.kit> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const data = s.backend.readFile(file);
  if (!data) throw new Error(`cannot read kit: ${file}`);
  rom.importKitFile(parseInt(idxStr, 10), data);
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into kit ${idxStr}; wrote ${out}`);
}

// --- patch: realize a whole manifest (the same schema build-kit reads a single entry of), one pass ---
//
// Each asset entry is either a BUILD/IMPORT (from a file) or a metadata TWEAK, dispatched to the same
// operations the individual verbs use. `slot` is the asset index. All mutation is in-memory; the ROM is
// only written on full success, so a bad entry fails the whole patch with nothing half-written.

function readOrThrow(s: Session, path: string, what: string): Uint8Array {
  const data = s.backend.readFile(path);
  if (!data) throw new Error(`cannot read ${what}: ${path}`);
  return data;
}

function patchManifest(s: Session, args: string[]): void {
  const [romPath, manifestPath, outRom] = args.filter((a) => !a.startsWith("--"));
  if (!romPath || !manifestPath || !outRom) throw new Error("usage: lsdj-rom patch <rom> <manifest.json> <out>");
  const rom = openRom(s, romPath);
  const m = JSON.parse(new TextDecoder().decode(readOrThrow(s, manifestPath, "manifest"))) as Manifest;
  let applied = 0;

  for (const ke of m.kits ?? []) {
    if (ke.slot == null) throw new Error('a manifest kit entry needs a "slot"');
    if (ke.build) {
      rom.setKitBank(ke.slot, compileKitBank(s, ke.name ?? "", ke.build, manifestPath, rom.rotatesSamples, effectsFromFlags(args)));
    } else if (ke.file) {
      rom.importKitFile(ke.slot, readOrThrow(s, resolvePath(manifestPath, ke.file), "kit"));
    }
    if (ke.name != null && !ke.build) rom.kit(ke.slot).setName(ke.name); // build already baked the name in
    for (const se of ke.samples ?? []) rom.kit(ke.slot).setSampleName(se.index, se.name);
    applied++;
  }

  const palettes = rom.palettes();
  for (const pe of m.palettes ?? []) {
    if (pe.file) {
      rom.importPaletteFile(pe.slot, readOrThrow(s, resolvePath(manifestPath, pe.file), "palette"));
    } else if (pe.set != null && pe.color != null && pe.rgb) {
      const p = palettes[pe.slot];
      if (!p) throw new Error(`palette ${pe.slot} out of range`);
      p.setColor(pe.set, pe.color, { r: pe.rgb[0], g: pe.rgb[1], b: pe.rgb[2] });
    } else {
      throw new Error(`palette entry (slot ${pe.slot}) needs either "file" or set/color/rgb`);
    }
    applied++;
  }

  const fonts = rom.fonts();
  for (const fe of m.fonts ?? []) {
    if (fe.file) {
      const img = s.backend.pngDecode(readOrThrow(s, resolvePath(manifestPath, fe.file), "font"));
      if (!img) throw new Error(`not a decodable PNG: ${fe.file}`);
      rom.importFontImage(fe.slot, img); // gfx auto-detected by image height
    } else if (fe.tile != null && fe.pixels) {
      const f = fonts[fe.slot];
      if (!f) throw new Error(`font ${fe.slot} out of range`);
      f.setTile(fe.tile, fe.pixels);
    } else {
      throw new Error(`font entry (slot ${fe.slot}) needs either "file" or tile/pixels`);
    }
    applied++;
  }

  if (!s.backend.writeFileAtomic(outRom, rom.bytes())) throw new Error(`write failed: ${outRom}`);
  console.log(`applied ${applied} manifest entr${applied === 1 ? "y" : "ies"}; wrote ${outRom}`);
}

const LSDJ_ROM_HELP = [
  "usage: retroplug-cli lsdj-rom <subcommand> ...",
  "",
  "  info           <rom> [--json]                     version + kit/palette/font inventory",
  "  extract        <rom> <outDir> [--rate N]          dump each kit sample to a mono WAV + rom.json",
  "  patch          <rom> <manifest.json> <out>        realize a manifest (builds/imports/metadata)",
  "  build-kit      <kit.json> <out.kit> [--no-rotate] compile a .kit from a (slotless) kit entry",
  "  import-sample  <rom> <kit> <audio> [flags]        compile one sample + splice into a kit",
  "  remove-sample  <rom> <kit> <slot> [--out]         drop a sample from a kit",
  "  export-palette <rom> <index> <out.lsdpal>         write palette <index> to a .lsdpal",
  "  import-palette <rom> <in.lsdpal> <index> [--out]  import a .lsdpal into palette <index>",
  "  export-font    <rom> <index> <out.png> [--gfx]    write font <index> to a .png (--gfx = extended)",
  "  import-font    <rom> <in.png> <index> [--out]     import a .png into font <index>",
  "  export-kit     <rom> <index> <out.kit>            write kit <index>'s 16 KB bank to a .kit",
  "  import-kit     <rom> <in.kit> <index> [--out]     import a .kit bank into kit <index>",
  "  (asset files are the community lsdpatch formats; font --gfx = the 46 shared extended tiles, 64x120)",
  "",
  "import-sample / build-kit effect flags: --gain X  --no-normalize",
  "                     --dither HighPassTPDF|ShapedTPDF|ErrorDiffusion|JJN|SierraLite  --no-dither",
  "                     --filter LowPass|HighPass|…  --cutoff HZ  --q Q",
  "",
  "ONE schema for patch (whole ROM) and build-kit (a single, slotless kit entry). Every entry",
  "either BUILDS/IMPORTS from a file or TWEAKS metadata; `slot` = the asset index.",
  '  { "kits":     [{ "slot": 20, "name": "DRUMS", "build": ["kick.wav", {"file":"sn.wav","name":"SN"}] },',
  '                 { "slot": 30, "file": "DONK.kit" },',
  '                 { "slot": 5,  "name": "RENAMED", "samples": [{ "index": 0, "name": "BD" }] }],',
  '    "palettes": [{ "slot": 1, "file": "x.lsdpal" }, { "slot": 0, "set": 0, "color": 0, "rgb": [255,0,0] }],',
  '    "fonts":    [{ "slot": 0, "file": "x.png" }, { "slot": 1, "tile": 3, "pixels": [ /* 64 */ ] }] }',
  "build-kit takes ONE kit entry without a slot, e.g.",
  '  { "name": "MYKIT", "build": [{ "file": "kick.wav", "name": "BD" }, "snare.wav"] }   → then import-kit',
  "  (build source: a path string, or { file, name?, offset?, length?, effects? })",
].join("\n");

export const lsdjRomTool: CliTool = {
  name: "lsdj-rom",
  summary: "inspect / extract / metadata-patch LSDj ROM kits, palettes and fonts",
  help: LSDJ_ROM_HELP,
  run(s: Session, args: string[]): void {
    const sub = args[0];
    const rest = args.slice(1);
    if (sub === "info") return info(s, rest);
    if (sub === "extract") return extract(s, rest);
    if (sub === "patch") return patchManifest(s, rest);
    if (sub === "build-kit") return buildKit(s, rest);
    if (sub === "import-sample") return importSample(s, rest);
    if (sub === "remove-sample") return removeSample(s, rest);
    if (sub === "export-palette") return exportPalette(s, rest);
    if (sub === "import-palette") return importPalette(s, rest);
    if (sub === "export-font") return exportFont(s, rest);
    if (sub === "import-font") return importFont(s, rest);
    if (sub === "export-kit") return exportKit(s, rest);
    if (sub === "import-kit") return importKit(s, rest);
    throw new Error(`unknown subcommand '${sub ?? ""}'\n\n${LSDJ_ROM_HELP}`);
  },
};
