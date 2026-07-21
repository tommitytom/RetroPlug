// `retroplug-cli evermidi-rom` — inspect, extract and edit the static assets (DPCM sample kits, the theme,
// the CHR font) inside an EverMIDI `.nes` ROM, headlessly. The EverMIDI twin of ./risa-rom.ts (EverMIDI is
// also NES/DMC, so it reuses the exact same risa asset codecs), backed by the pure-TS src/evermidi/rom
// module; this is just the CLI surface (arg parsing + file I/O + WAV/JSON output). Kit compile is native
// (s.audio.compileDmc — the generic DMC compiler); sample splicing re-packs existing DPCM + a freshly-
// compiled slot via assembleKitBank (byte-identical to a whole-kit recompile).
//
// EverMIDI has up to 16 SWITCHABLE kit banks on the banking builds (VRC6/VRC7/S5B/FME-7/N163) and a single
// kit on NROM — kit indices are bounded by rom.kitBankCapacity(). It bakes ONE theme (index 0) and one CHR
// font. Unlike risa there is NO kit-metadata mirror, so setKit is a plain bank splice.
//
//   retroplug-cli evermidi-rom info          <rom> [--json]
//   retroplug-cli evermidi-rom extract       <rom> <outDir> [--rate N]
//   retroplug-cli evermidi-rom patch         <rom> <manifest.json> <out>       (the whole-ROM manifest)
//   retroplug-cli evermidi-rom build-kit     <kit.json> <out.rkit> [flags]     (native compile → .rkit file)
//   retroplug-cli evermidi-rom import-sample <rom> <kit> <audio> [flags]       (compile one + splice)
//   retroplug-cli evermidi-rom remove-sample <rom> <kit> <slot> [--out rom]
//   retroplug-cli evermidi-rom export-theme  / import-theme   (.rit palette-role JSON)
//   retroplug-cli evermidi-rom export-font   / import-font    (.chr raw 8 KB CHR banks)
//   retroplug-cli evermidi-rom export-kit    / import-kit     (.rkit raw 8 KB DPCM banks)
import type { CliTool } from "../tools";
import type { Session } from "../session";
import type { KitEffect, RisaDmcSampleSpec } from "../../src/audioDriver";
import { encodeWav } from "../wav";
import { EverMidiRom } from "../../src/evermidi/rom";
import {
  bankToModel,
  isBankPopulated,
  assembleKitBank,
  dpcmDecode,
  serializeRit,
  parseRit,
  decodeThemeFromRom,
  normalizeTheme,
  encodeThemeRecord,
  encodeThemeName,
  PAL_DPCM_RATES_HZ,
  KIT_BANK_SIZE,
  KIT_SLOT_COUNT,
  CHR_BANK_SIZE,
  type AssembleSlot,
} from "../../src/risa/rom";

const enc = new TextEncoder();
const dec = new TextDecoder();
const sanitize = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "_";
const pad2 = (n: number): string => String(n).padStart(2, "0");

function openRom(s: Session, path: string): EverMidiRom {
  const bytes = s.backend.readFile(path);
  if (!bytes) throw new Error(`cannot read ROM: ${path}`);
  const rom = EverMidiRom.fromBytes(bytes);
  if (!rom.isEverMidi) throw new Error(`not a recognised EverMIDI ROM (size=${bytes.length})`);
  return rom;
}

// Validate a kit-bank index against the ROM's capacity (1 on NROM, up to 16 on a banking build). EverMidiRom
// setKit silently no-ops out of range, so the verbs that splice a bank directly must guard first.
function kitIndexInRange(rom: EverMidiRom, idx: number): number {
  const cap = rom.kitBankCapacity();
  if (!Number.isInteger(idx) || idx < 0 || idx >= cap) throw new Error(`kit index ${idx} out of range (0..${cap - 1})`);
  return idx;
}

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};
const has = (args: string[], name: string): boolean => args.includes(name);
const positionals = (args: string[]): string[] => {
  // Tokens that aren't a flag and aren't a flag's value. Only value-taking flags consume the next token.
  const valueFlags = new Set(["--out", "--name", "--slot", "--rate", "--gain", "--filter", "--cutoff", "--q"]);
  const out: string[] = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i].startsWith("--")) {
      if (valueFlags.has(args[i])) i++;
      continue;
    }
    out.push(args[i]);
  }
  return out;
};

function readOrThrow(s: Session, path: string, what: string): Uint8Array {
  const data = s.backend.readFile(path);
  if (!data) throw new Error(`cannot read ${what}: ${path}`);
  return data;
}

// --- kit compile (native compileDmc) --------------------------------------------------------------------

// Per-sample shaping. The DMC codec skips dither (1-bit only) — so gain/filter only. --no-normalize maps
// to the DMC 7-bit normalize (spec.normalize) rather than a gain effect, so there's no double-normalize.
function effectsFromFlags(args: string[]): KitEffect[] {
  const fx: KitEffect[] = [];
  const gain = flag(args, "--gain");
  if (gain != null) fx.push({ type: "gain", normalize: false, gain: Number(gain) });
  const filter = flag(args, "--filter");
  if (filter) {
    fx.push({
      type: "filter",
      frequency: flag(args, "--cutoff") != null ? Number(flag(args, "--cutoff")) : 5734,
      q: flag(args, "--q") != null ? Number(flag(args, "--q")) : 1,
      filterType: filter,
    } as KitEffect);
  }
  return fx;
}

// The CLI fallbacks a build source uses when it doesn't carry its own value.
interface Fallbacks {
  effects: KitEffect[];
  rate?: number;
  loop: boolean;
  normalize: boolean;
}
function fallbacks(args: string[]): Fallbacks {
  return {
    effects: effectsFromFlags(args),
    rate: flag(args, "--rate") != null ? Number(flag(args, "--rate")) : undefined,
    loop: has(args, "--loop"),
    normalize: !has(args, "--no-normalize"),
  };
}

function sampleName(file: string): string {
  return file.slice(file.lastIndexOf("/") + 1).replace(/\.[^.]*$/, "");
}
function resolvePath(baseFile: string, p: string): string {
  if (p.startsWith("/")) return p;
  return baseFile.slice(0, baseFile.lastIndexOf("/") + 1) + p;
}

type BuildSource = string | { file: string; name?: string; offset?: number; length?: number; effects?: KitEffect[]; rate?: number; loop?: boolean; normalize?: boolean };
function normalizeBuild(build: BuildSource[]): Exclude<BuildSource, string>[] {
  return build.map((b) => (typeof b === "string" ? { file: b } : b));
}

// Compile a whole 8 KB DPCM bank from `build` sources, asserting every source landed (a bad path silently
// leaves an empty slot natively). Shared by build-kit + patch.
function compileKitBank(s: Session, name: string, build: BuildSource[], baseFile: string, fb: Fallbacks): Uint8Array {
  const sources = normalizeBuild(build);
  const samples: RisaDmcSampleSpec[] = sources.map((sm) => ({
    path: resolvePath(baseFile, sm.file),
    name: sm.name ?? sampleName(sm.file),
    offset: sm.offset,
    length: sm.length,
    effects: sm.effects ?? fb.effects,
    rate: sm.rate ?? fb.rate,
    loop: sm.loop ?? fb.loop,
    normalize: sm.normalize ?? fb.normalize,
  }));
  const bank = s.audio.compileDmc({ name, samples });
  if (bank.length !== KIT_BANK_SIZE) throw new Error("compileDmc returned an unexpected bank size");
  const model = bankToModel(bank);
  sources.forEach((sm, i) => {
    if (!model.slots[i]) throw new Error(`sample ${i} (${sm.file}) failed to compile — check the path/format`);
  });
  return bank;
}

// --- manifest schema (patch whole-ROM + build-kit single entry) -----------------------------------------

interface KitEntry {
  slot?: number;
  name?: string;
  build?: BuildSource[];
  file?: string; // import a .rkit bank
  samples?: { index: number; name: string }[]; // renames
}
interface ThemeEntry {
  slot: number;
  file: string; // import a .rit
}
interface FontEntry {
  slot: number;
  file: string; // import a .chr
}
interface Manifest {
  kits?: KitEntry[];
  themes?: ThemeEntry[];
  fonts?: FontEntry[];
}

// The kit's 16 sample slots as re-packable AssembleSlots (null where empty), for splice/rename.
function kitSlots(rom: EverMidiRom, kitIndex: number): { name: string; slots: (AssembleSlot | null)[] } {
  const bank = rom.getKitBank(kitIndex);
  if (!bank) throw new Error(`kit index ${kitIndex} out of range`);
  const model = bankToModel(bank);
  return {
    name: model.name,
    slots: model.slots.map((sl) => (sl ? { dpcm: sl.dpcm, rate: sl.rate, loop: sl.loop, name: sl.name } : null)),
  };
}

// Apply kit metadata renames (kit name and/or per-sample names) by re-assembling the current bank.
function applyKitRenames(rom: EverMidiRom, slot: number, newName: string | undefined, renames: { index: number; name: string }[] | undefined): void {
  const { name, slots } = kitSlots(rom, slot);
  for (const r of renames ?? []) if (slots[r.index]) slots[r.index]!.name = r.name;
  rom.setKit(slot, assembleKitBank(newName ?? name, slots));
}

// --- verbs ----------------------------------------------------------------------------------------------

function romToJson(rom: EverMidiRom): unknown {
  const themes = rom.themes().map((t) => ({ slot: t.slot, theme: serializeRit(t.theme).theme }));
  const fonts = rom.fonts().map((f) => ({ slot: f.slot }));
  const kits = rom.kits().map((k) => ({
    slot: k.slot,
    name: k.name,
    samples: k.model.slots.map((sm, i) => (sm ? { slot: i, name: sm.name, rate: sm.rate, loop: sm.loop, bytes: sm.dpcm.length } : null)).filter(Boolean),
  }));
  return { kitBanks: rom.kitBankCapacity(), themes, fonts, kits };
}

function info(s: Session, args: string[]): void {
  const romPath = positionals(args)[0];
  if (!romPath) throw new Error("usage: evermidi-rom info <rom> [--json]");
  const rom = openRom(s, romPath);
  if (has(args, "--json")) {
    console.log(JSON.stringify(romToJson(rom), null, 2));
    return;
  }
  const themes = rom.themes();
  console.log(`themes: ${themes.length}`);
  for (const t of themes) console.log(`  [${String(t.slot).padStart(2)}] ${t.theme.name.trim() || "(unnamed)"}`);
  const fonts = rom.fonts();
  console.log(`fonts: ${fonts.length}`);
  const kits = rom.kits();
  console.log(`kits: ${kits.length} populated / ${rom.kitBankCapacity()} banks`);
  for (const k of kits) console.log(`  [${String(k.slot).padStart(2)}] ${k.name.padEnd(6)}  ${k.model.slots.filter(Boolean).length} samples`);
}

function extract(s: Session, args: string[]): void {
  const [romPath, outDir] = positionals(args);
  if (!romPath || !outDir) throw new Error("usage: evermidi-rom extract <rom> <outDir> [--rate N]");
  const rateOverride = flag(args, "--rate") != null ? parseInt(flag(args, "--rate")!, 10) : undefined;
  const rom = openRom(s, romPath);
  const write = (name: string, bytes: Uint8Array) => {
    if (!s.backend.writeFile(`${outDir}/${name}`, bytes)) throw new Error(`write failed: ${outDir}/${name}`);
  };

  write("rom.json", enc.encode(JSON.stringify(romToJson(rom), null, 2)));
  for (const t of rom.themes())
    write(`theme${pad2(t.slot)}_${sanitize(t.theme.name)}.rit`, enc.encode(JSON.stringify(serializeRit(t.theme), null, 2) + "\n"));
  for (const f of rom.fonts()) {
    const bank = rom.getChrFontSlot(f.slot);
    if (bank) write(`font${pad2(f.slot)}.chr`, bank);
  }

  let wavs = 0;
  for (const k of rom.kits()) {
    const kitName = sanitize(k.name);
    k.model.slots.forEach((sm, i) => {
      if (!sm) return;
      const rate = rateOverride ?? Math.round(PAL_DPCM_RATES_HZ[sm.rate] ?? PAL_DPCM_RATES_HZ[12]);
      write(`kit${pad2(k.slot)}_${kitName}_${pad2(i)}_${sanitize(sm.name)}.wav`, encodeWav(dpcmDecode(sm.dpcm), rate, 1));
      wavs++;
    });
  }
  console.log(`extracted ${wavs} sample WAVs + ${rom.themes().length} themes + ${rom.fonts().length} fonts + rom.json to ${outDir}`);
}

function buildKit(s: Session, args: string[]): void {
  const [specPath, outKit] = positionals(args);
  if (!specPath || !outKit) throw new Error("usage: evermidi-rom build-kit <kit.json> <out.rkit> [effect flags]");
  const spec = JSON.parse(dec.decode(readOrThrow(s, specPath, "kit spec"))) as KitEntry;
  if (!spec.build || spec.build.length === 0) throw new Error('kit spec needs a "build" array of source audio files');
  const bank = compileKitBank(s, spec.name ?? "", spec.build, specPath, fallbacks(args));
  if (!s.backend.writeFile(outKit, bank)) throw new Error(`write failed: ${outKit}`);
  console.log(`built kit "${spec.name ?? ""}" (${spec.build.length} samples); wrote ${outKit}`);
}

function importSample(s: Session, args: string[]): void {
  const [romPath, kitStr, audio] = positionals(args);
  if (!romPath || kitStr == null || !audio) throw new Error("usage: evermidi-rom import-sample <rom> <kit> <audio> [--slot N] [--name X] [--out rom] [flags]");
  const rom = openRom(s, romPath);
  const kitIndex = kitIndexInRange(rom, parseInt(kitStr, 10));
  const name = flag(args, "--name") ?? sampleName(audio);
  const fb = fallbacks(args);

  // Compile the one sample natively (a throwaway 1-sample bank) and pull its DPCM back out.
  const oneBank = s.audio.compileDmc({ name: "", samples: [{ path: audio, name, effects: fb.effects, rate: fb.rate, loop: fb.loop, normalize: fb.normalize }] });
  const one = bankToModel(oneBank).slots[0];
  if (!one) throw new Error(`failed to compile ${audio} — check the path/format`);

  const { name: kitName, slots } = kitSlots(rom, kitIndex);
  const slotStr = flag(args, "--slot");
  let slot: number;
  if (slotStr == null || slotStr === "next") {
    slot = slots.findIndex((x) => x == null);
    if (slot < 0) throw new Error("kit is full (16 samples) — target a slot with --slot N");
  } else {
    slot = parseInt(slotStr, 10);
    if (slot < 0 || slot >= KIT_SLOT_COUNT) throw new Error(`--slot ${slot} out of range (0..${KIT_SLOT_COUNT - 1})`);
  }
  slots[slot] = { dpcm: one.dpcm, rate: one.rate, loop: one.loop, name };

  rom.setKit(kitIndex, assembleKitBank(kitName, slots));
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${name} into kit ${kitIndex} slot ${slot}; wrote ${out}`);
}

function removeSample(s: Session, args: string[]): void {
  const [romPath, kitStr, slotStr] = positionals(args);
  if (!romPath || kitStr == null || slotStr == null) throw new Error("usage: evermidi-rom remove-sample <rom> <kit> <slot> [--out rom]");
  const rom = openRom(s, romPath);
  const kitIndex = kitIndexInRange(rom, parseInt(kitStr, 10));
  const slot = parseInt(slotStr, 10);
  const { name, slots } = kitSlots(rom, kitIndex);
  if (slot < 0 || slot >= KIT_SLOT_COUNT || !slots[slot]) throw new Error(`slot ${slot} is not populated`);
  slots[slot] = null; // empty the slot — its index is preserved (kit slots are index-addressed)
  rom.setKit(kitIndex, assembleKitBank(name, slots));
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`removed slot ${slot} from kit ${kitIndex}; wrote ${out}`);
}

function exportTheme(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = positionals(args);
  if (!romPath || idxStr == null || !out) throw new Error("usage: evermidi-rom export-theme <rom> <index> <out.rit>");
  const rom = openRom(s, romPath);
  const t = rom.getTheme(parseInt(idxStr, 10));
  if (!t) throw new Error(`theme ${idxStr} out of range / no theme table`);
  const theme = decodeThemeFromRom(t.recordBytes, t.nameBytes);
  if (!s.backend.writeFile(out, enc.encode(JSON.stringify(serializeRit(theme), null, 2) + "\n"))) throw new Error(`write failed: ${out}`);
  console.log(`wrote theme ${idxStr} to ${out}`);
}

function importTheme(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = positionals(args);
  if (!romPath || !file || idxStr == null) throw new Error("usage: evermidi-rom import-theme <rom> <in.rit> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const { theme } = parseRit(JSON.parse(dec.decode(readOrThrow(s, file, "theme .rit")))); // throws on a bad .rit
  const t = normalizeTheme(theme);
  rom.setTheme(parseInt(idxStr, 10), encodeThemeRecord(t), encodeThemeName(t));
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into theme ${idxStr}; wrote ${out}`);
}

function exportFont(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = positionals(args);
  if (!romPath || idxStr == null || !out) throw new Error("usage: evermidi-rom export-font <rom> <index> <out.chr>");
  const rom = openRom(s, romPath);
  const bank = rom.getChrFontSlot(parseInt(idxStr, 10));
  if (!bank) throw new Error(`font ${idxStr} out of range / no CHR region`);
  if (!s.backend.writeFile(out, bank)) throw new Error(`write failed: ${out}`);
  console.log(`wrote font ${idxStr} (8 KB CHR bank) to ${out}`);
}

function importFont(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = positionals(args);
  if (!romPath || !file || idxStr == null) throw new Error("usage: evermidi-rom import-font <rom> <in.chr> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const data = readOrThrow(s, file, "font .chr");
  if (data.length !== CHR_BANK_SIZE) throw new Error(`.chr must be exactly 8 KB (got ${data.length})`);
  rom.setChrFontSlot(parseInt(idxStr, 10), data);
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into font ${idxStr}; wrote ${out}`);
}

function exportKit(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = positionals(args);
  if (!romPath || idxStr == null || !out) throw new Error("usage: evermidi-rom export-kit <rom> <index> <out.rkit>");
  const rom = openRom(s, romPath);
  const bank = rom.getKitBank(parseInt(idxStr, 10));
  if (!bank || !isBankPopulated(bank)) throw new Error(`kit ${idxStr} is empty / out of range`);
  if (!s.backend.writeFile(out, bank)) throw new Error(`write failed: ${out}`);
  console.log(`wrote kit ${idxStr} (8 KB DPCM bank) to ${out}`);
}

function importKit(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = positionals(args);
  if (!romPath || !file || idxStr == null) throw new Error("usage: evermidi-rom import-kit <rom> <in.rkit> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const idx = kitIndexInRange(rom, parseInt(idxStr, 10));
  const data = readOrThrow(s, file, "kit .rkit");
  if (data.length !== KIT_BANK_SIZE || !isBankPopulated(data)) throw new Error(`.rkit must be a populated 8 KB DPCM bank`);
  rom.setKit(idx, data); // plain bank splice — EverMIDI reads the kit index directly at boot (no mirror)
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into kit ${idx}; wrote ${out}`);
}

function patchManifest(s: Session, args: string[]): void {
  const [romPath, manifestPath, outRom] = positionals(args);
  if (!romPath || !manifestPath || !outRom) throw new Error("usage: evermidi-rom patch <rom> <manifest.json> <out>");
  const rom = openRom(s, romPath);
  const m = JSON.parse(dec.decode(readOrThrow(s, manifestPath, "manifest"))) as Manifest;
  const fb = fallbacks(args);
  let applied = 0;

  for (const ke of m.kits ?? []) {
    if (ke.slot == null) throw new Error('a manifest kit entry needs a "slot"');
    kitIndexInRange(rom, ke.slot);
    if (ke.build) {
      rom.setKit(ke.slot, compileKitBank(s, ke.name ?? "", ke.build, manifestPath, fb));
    } else if (ke.file) {
      const bank = readOrThrow(s, resolvePath(manifestPath, ke.file), "kit");
      if (bank.length !== KIT_BANK_SIZE || !isBankPopulated(bank)) throw new Error(`kit file ${ke.file}: not a populated 8 KB .rkit`);
      rom.setKit(ke.slot, bank);
    }
    // Metadata renames (a bare-metadata entry, or renames layered on a build/import). `build` already baked
    // the kit name, so only rename it when there was no build.
    if ((ke.name != null && !ke.build) || (ke.samples && ke.samples.length)) {
      applyKitRenames(rom, ke.slot, ke.name != null && !ke.build ? ke.name : undefined, ke.samples);
    }
    applied++;
  }

  for (const te of m.themes ?? []) {
    if (!te.file) throw new Error(`theme entry (slot ${te.slot}) needs "file"`);
    const { theme } = parseRit(JSON.parse(dec.decode(readOrThrow(s, resolvePath(manifestPath, te.file), "theme"))));
    const t = normalizeTheme(theme);
    rom.setTheme(te.slot, encodeThemeRecord(t), encodeThemeName(t));
    applied++;
  }

  for (const fe of m.fonts ?? []) {
    if (!fe.file) throw new Error(`font entry (slot ${fe.slot}) needs "file"`);
    const data = readOrThrow(s, resolvePath(manifestPath, fe.file), "font");
    if (data.length !== CHR_BANK_SIZE) throw new Error(`font ${fe.file}: .chr must be exactly 8 KB`);
    rom.setChrFontSlot(fe.slot, data);
    applied++;
  }

  if (!s.backend.writeFileAtomic(outRom, rom.bytes())) throw new Error(`write failed: ${outRom}`);
  console.log(`applied ${applied} manifest entr${applied === 1 ? "y" : "ies"}; wrote ${outRom}`);
}

const EVERMIDI_ROM_HELP = [
  "usage: retroplug-cli evermidi-rom <subcommand> ...",
  "",
  "  info          <rom> [--json]                     theme / font / kit inventory (+ kit-bank capacity)",
  "  extract       <rom> <outDir> [--rate N]          dump each kit sample to a mono WAV + theme/font + rom.json",
  "  patch         <rom> <manifest.json> <out>        realize a manifest (builds/imports/metadata)",
  "  build-kit     <kit.json> <out.rkit> [flags]      compile a .rkit from a (slotless) kit entry",
  "  import-sample <rom> <kit> <audio> [flags]        compile one sample + splice into a kit",
  "  remove-sample <rom> <kit> <slot> [--out]         empty a kit slot (index preserved)",
  "  export-theme  <rom> <index> <out.rit>            write theme <index> to a .rit (palette-role JSON)",
  "  import-theme  <rom> <in.rit> <index> [--out]     import a .rit into theme <index>",
  "  export-font   <rom> <index> <out.chr>            write font <index>'s 8 KB CHR bank to a .chr",
  "  import-font   <rom> <in.chr> <index> [--out]     import a .chr bank into font <index>",
  "  export-kit    <rom> <index> <out.rkit>           write kit <index>'s 8 KB DPCM bank to a .rkit",
  "  import-kit    <rom> <in.rkit> <index> [--out]    import a .rkit bank into kit <index>",
  "  (themes are risa's .rit palette-role JSON; fonts are raw 8 KB NES CHR banks; kits are 8 KB DPCM banks)",
  "  (kit <index> is a switchable ROM bank: 0 on NROM, 0..15 on a banking build — see `info`)",
  "",
  "build-kit / import-sample flags:  --rate N (PAL DPCM index 0-15, default 12)  --loop  --no-normalize",
  "                     --gain X  --filter LowPass|HighPass|…  --cutoff HZ  --q Q   (no dither — DMC is 1-bit)",
  "",
  "ONE schema for patch (whole ROM) and build-kit (a single, slotless kit entry). Every entry either",
  "BUILDS/IMPORTS from a file or TWEAKS metadata; `slot` = the asset index.",
  '  { "kits":   [{ "slot": 0, "name": "DRUMS", "build": ["kick.wav", {"file":"sn.wav","name":"SN","rate":12}] },',
  '               { "slot": 1, "file": "HAT.rkit" },',
  '               { "slot": 2, "name": "RENAMED", "samples": [{ "index": 0, "name": "BD" }] }],',
  '    "themes": [{ "slot": 0, "file": "dark.rit" }],',
  '    "fonts":  [{ "slot": 0, "file": "big.chr" }] }',
  "build-kit takes ONE kit entry without a slot, e.g.",
  '  { "name": "MYKIT", "build": [{ "file": "kick.wav", "name": "BD", "rate": 12 }, "snare.wav"] }   → then import-kit',
  "  (build source: a path string, or { file, name?, rate?, loop?, normalize?, offset?, length?, effects? })",
].join("\n");

export const everMidiRomTool: CliTool = {
  name: "evermidi-rom",
  summary: "inspect / extract / edit EverMIDI ROM kits, theme and font (+ compile WAV → .rkit)",
  help: EVERMIDI_ROM_HELP,
  run(s: Session, args: string[]): void {
    const sub = args[0];
    const rest = args.slice(1);
    if (sub === "info") return info(s, rest);
    if (sub === "extract") return extract(s, rest);
    if (sub === "patch") return patchManifest(s, rest);
    if (sub === "build-kit") return buildKit(s, rest);
    if (sub === "import-sample") return importSample(s, rest);
    if (sub === "remove-sample") return removeSample(s, rest);
    if (sub === "export-theme") return exportTheme(s, rest);
    if (sub === "import-theme") return importTheme(s, rest);
    if (sub === "export-font") return exportFont(s, rest);
    if (sub === "import-font") return importFont(s, rest);
    if (sub === "export-kit") return exportKit(s, rest);
    if (sub === "import-kit") return importKit(s, rest);
    throw new Error(`unknown subcommand '${sub ?? ""}'\n\n${EVERMIDI_ROM_HELP}`);
  },
};
