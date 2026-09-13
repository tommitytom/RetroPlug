// `retroplug-cli bliptoaster-rom` — inspect, extract and edit the static assets (DPCM sample kits, the theme,
// the CHR font) inside a BlipToaster `.nes` ROM, headlessly. The BlipToaster twin of ./risa-rom.ts (BlipToaster is
// also NES/DMC, so it reuses the exact same risa asset codecs), backed by the pure-TS src/bliptoaster/rom
// module; this is just the CLI surface (arg parsing + file I/O + WAV/JSON output). Kit compile is native
// (s.audio.compileDmc — the generic DMC compiler); sample splicing re-packs existing DPCM + a freshly-
// compiled slot via assembleKitBank (byte-identical to a whole-kit recompile).
//
// BlipToaster has up to 16 SWITCHABLE kit banks on the banking builds (VRC6/VRC7/S5B/FME-7/N163) and a single
// kit on NROM — kit indices are bounded by rom.kitBankCapacity(). It also bakes 16 themes (bounded by
// rom.themeCount) and 4 CHR fonts (rom.chrFontSlotCount), all switchable live over MIDI. Unlike risa there is
// NO kit-metadata mirror, so setKit is a plain bank splice.
//
//   retroplug-cli bliptoaster-rom info          <rom> [--json]
//   retroplug-cli bliptoaster-rom extract       <rom> <outDir> [--rate N]
//   retroplug-cli bliptoaster-rom patch         <rom> <manifest.json> <out>       (the whole-ROM manifest)
//   retroplug-cli bliptoaster-rom build-kit     <kit.json> <out.rkit> [flags]     (native compile → .rkit file)
//   retroplug-cli bliptoaster-rom import-sample <rom> <kit> <audio> [flags]       (compile one + splice)
//   retroplug-cli bliptoaster-rom remove-sample <rom> <kit> <slot> [--out rom]
//   retroplug-cli bliptoaster-rom export-theme  / import-theme   (.rit palette-role JSON)
//   retroplug-cli bliptoaster-rom export-font   / import-font    (.chr raw 8 KB CHR banks)
//   retroplug-cli bliptoaster-rom export-kit    / import-kit     (.rkit raw 8 KB DPCM banks)
import type { CliTool } from "../tools";
import type { Session } from "../session";
import type { KitEffect, RisaDmcSampleSpec } from "../../src/audioDriver";
import { encodeWav } from "../wav";
import {
  BlipToasterRom,
  SETTINGS_KIT_COUNT,
  SETTINGS_THEME_COUNT,
  SETTINGS_FONT_COUNT,
  type BlipToasterSettingsPatch,
} from "../../src/bliptoaster/rom";
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

function openRom(s: Session, path: string): BlipToasterRom {
  const bytes = s.backend.readFile(path);
  if (!bytes) throw new Error(`cannot read ROM: ${path}`);
  const rom = BlipToasterRom.fromBytes(bytes);
  if (!rom.isBlipToaster) throw new Error(`not a recognised BlipToaster ROM (size=${bytes.length})`);
  return rom;
}

// Validate a kit-bank index against the ROM's capacity (1 on NROM, up to 16 on a banking build). BlipToasterRom
// setKit silently no-ops out of range, so the verbs that splice a bank directly must guard first.
function kitIndexInRange(rom: BlipToasterRom, idx: number): number {
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
  const valueFlags = new Set([
    "--out", "--name", "--slot", "--rate", "--gain", "--filter", "--cutoff", "--q",
    // `settings` flags: each takes a value, which must not be mistaken for the <rom> positional.
    "--base-channel", "--kit", "--ppu", "--mode1", "--curve", "--theme", "--font",
  ]);
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
  // The baked rig settings, as the `settings` verb's fields. Named fields only, so a manifest can pin the
  // default kit without restating the whole block.
  settings?: BlipToasterSettingsPatch;
}

// The kit's 16 sample slots as re-packable AssembleSlots (null where empty), for splice/rename.
function kitSlots(rom: BlipToasterRom, kitIndex: number): { name: string; slots: (AssembleSlot | null)[] } {
  const bank = rom.getKitBank(kitIndex);
  if (!bank) throw new Error(`kit index ${kitIndex} out of range`);
  const model = bankToModel(bank);
  return {
    name: model.name,
    slots: model.slots.map((sl) => (sl ? { dpcm: sl.dpcm, rate: sl.rate, loop: sl.loop, name: sl.name } : null)),
  };
}

// Apply kit metadata renames (kit name and/or per-sample names) by re-assembling the current bank.
function applyKitRenames(rom: BlipToasterRom, slot: number, newName: string | undefined, renames: { index: number; name: string }[] | undefined): void {
  const { name, slots } = kitSlots(rom, slot);
  for (const r of renames ?? []) if (slots[r.index]) slots[r.index]!.name = r.name;
  rom.setKit(slot, assembleKitBank(newName ?? name, slots));
}

// --- verbs ----------------------------------------------------------------------------------------------

function romToJson(rom: BlipToasterRom): unknown {
  const themes = rom.themes().map((t) => ({ slot: t.slot, theme: serializeRit(t.theme).theme }));
  const fonts = rom.fonts().map((f) => ({ slot: f.slot }));
  const kits = rom.kits().map((k) => ({
    slot: k.slot,
    name: k.name,
    samples: k.model.slots.map((sm, i) => (sm ? { slot: i, name: sm.name, rate: sm.rate, loop: sm.loop, bytes: sm.dpcm.length } : null)).filter(Boolean),
  }));
  // `settings` is null on a ROM with no readable block (one predating it, or stamped a newer format).
  return { kitBanks: rom.kitBankCapacity(), settings: rom.settings(), themes, fonts, kits };
}

// The baked settings as `info` prints them: the field name, then the effective value with the entry it names
// where there is one (the default kit and theme point into the tables above).
function settingsLines(rom: BlipToasterRom): string[] {
  const set = rom.settings();
  if (!set) return ["settings: (no block)"];
  const named = (slot: number, name: string | undefined): string => `${slot}${name ? ` (${name})` : ""}`;
  return [
    "settings:",
    `  base channel:    BASE${String(set.baseChannel + 1).padStart(2, "0")}`,
    `  default kit:     ${named(set.kit, rom.kits().find((k) => k.slot === set.kit)?.name)}`,
    `  ppu enabled:     ${set.ppu ? "yes" : "no"}`,
    `  velocity curve:  ${set.velCurve ? "log" : "linear"}`,
    // A field this image's build predates reads as its default but is not actually honoured, so say so rather
    // than printing a value the cart will not act on.
    `  default theme:   ${named(set.theme, rom.themes().find((t) => t.slot === set.theme)?.theme.name.trim())}${rom.settingSupported("theme") ? "" : "   (not read by this ROM - predates the field)"}`,
    `  default font:    ${set.font}${rom.settingSupported("font") ? "" : "   (not read by this ROM - predates the field)"}`,
  ];
}

function info(s: Session, args: string[]): void {
  const romPath = positionals(args)[0];
  if (!romPath) throw new Error("usage: bliptoaster-rom info <rom> [--json]");
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
  for (const line of settingsLines(rom)) console.log(line);
}

// --- settings: print the baked block, or patch named fields into a copy -----------------------------------
// Every field is a BOOT default the cart's own CCs still move live; the ROM clamps anything out of range back
// to that field's power-on default, and so does the writer (src/bliptoaster/rom/settings.ts), so the value
// printed back is always the one the cart will really boot with.
const SETTINGS_FLAGS: Record<string, string> = {
  "--base-channel": "1-16",
  "--kit": "0-15",
  "--ppu": "on|off",
  "--mode1": "on|off",
  "--curve": "linear|log",
  "--theme": "0-15",
  "--font": "0-3",
};

// Parse one flag into its settings field, throwing with the accepted range rather than silently clamping - a CLI
// typo should not quietly bake a different value than the one asked for.
function intFlag(args: string[], name: string, max: number, offset = 0): number | undefined {
  const raw = flag(args, name);
  if (raw == null) return undefined;
  const n = parseInt(raw, 10) - offset;
  if (!Number.isInteger(n) || n < 0 || n > max) throw new Error(`${name} must be ${SETTINGS_FLAGS[name]}`);
  return n;
}
function enumFlag(args: string[], name: string, on: string, off: string): boolean | undefined {
  const raw = flag(args, name);
  if (raw == null) return undefined;
  if (raw === on) return true;
  if (raw === off) return false;
  throw new Error(`${name} must be ${SETTINGS_FLAGS[name]}`);
}

/** The `+9` byte, from either spelling. It was "Mode 1 at boot" (on = dark until START) and is now "PPU
 *  enabled" (on = the screen draws) - the same byte with the opposite sense - so `--mode1` survives only as a
 *  DEPRECATED alias for the inverse, which is what keeps an old command line doing what it says rather than
 *  what it used to write. Giving both is refused: one byte, two spellings that disagree, and picking a winner
 *  silently is how someone bakes a dark screen and blames the cart. */
function ppuFlag(args: string[]): boolean | undefined {
  const ppu = enumFlag(args, "--ppu", "on", "off");
  const mode1 = enumFlag(args, "--mode1", "on", "off");
  if (ppu !== undefined && mode1 !== undefined) throw new Error("--ppu and --mode1 set the same byte; pass one (--mode1 is the deprecated inverse)");
  if (mode1 !== undefined) {
    console.log("warning: --mode1 is deprecated - the byte now means PPU enabled, so this writes --ppu " + (mode1 ? "off" : "on"));
    return !mode1;
  }
  return ppu;
}

function settings(s: Session, args: string[]): void {
  const romPath = positionals(args)[0];
  if (!romPath) throw new Error("usage: bliptoaster-rom settings <rom> [--base-channel 1-16] [--kit 0-15] [--ppu on|off] [--curve linear|log] [--theme 0-15] [--font 0-3] [--out <nes>]");
  const rom = openRom(s, romPath);
  if (!rom.hasSettings) throw new Error("no settings block in this ROM (it predates the block, or is stamped a format this build does not read)");

  const patch: BlipToasterSettingsPatch = {};
  const baseChannel = intFlag(args, "--base-channel", 15, 1); // 1-16 on the command line, 0-15 in the block
  const kit = intFlag(args, "--kit", SETTINGS_KIT_COUNT - 1);
  const ppu = ppuFlag(args);
  const curve = enumFlag(args, "--curve", "log", "linear");
  const theme = intFlag(args, "--theme", SETTINGS_THEME_COUNT - 1);
  const font = intFlag(args, "--font", SETTINGS_FONT_COUNT - 1);
  if (baseChannel !== undefined) patch.baseChannel = baseChannel;
  if (kit !== undefined) patch.kit = kit;
  if (ppu !== undefined) patch.ppu = ppu;
  if (curve !== undefined) patch.velCurve = curve;
  if (theme !== undefined) patch.theme = theme;
  if (font !== undefined) patch.font = font;

  // No flags = read-only. Print and write NOTHING, so `settings <rom>` can never touch the ROM it is inspecting.
  if (Object.keys(patch).length === 0) {
    for (const line of settingsLines(rom)) console.log(line);
    return;
  }
  // Refuse a field this image's own build predates. Its byte is still the reserved 0xFF, so writing it would
  // succeed, report success, and change nothing about how the cart boots.
  for (const field of ["theme", "font"] as const) {
    if (patch[field] !== undefined && !rom.settingSupported(field)) {
      throw new Error(`this ROM predates the baked ${field} setting (its byte is still reserved) - rebuild it from a source tree that has SET_F_${field.toUpperCase()}`);
    }
  }
  rom.setSettings(patch);
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`wrote ${Object.keys(patch).join(", ")} to ${out}`);
  for (const line of settingsLines(BlipToasterRom.fromBytes(rom.bytes()))) console.log(line);
}

function extract(s: Session, args: string[]): void {
  const [romPath, outDir] = positionals(args);
  if (!romPath || !outDir) throw new Error("usage: bliptoaster-rom extract <rom> <outDir> [--rate N]");
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
  if (!specPath || !outKit) throw new Error("usage: bliptoaster-rom build-kit <kit.json> <out.rkit> [effect flags]");
  const spec = JSON.parse(dec.decode(readOrThrow(s, specPath, "kit spec"))) as KitEntry;
  if (!spec.build || spec.build.length === 0) throw new Error('kit spec needs a "build" array of source audio files');
  const bank = compileKitBank(s, spec.name ?? "", spec.build, specPath, fallbacks(args));
  if (!s.backend.writeFile(outKit, bank)) throw new Error(`write failed: ${outKit}`);
  console.log(`built kit "${spec.name ?? ""}" (${spec.build.length} samples); wrote ${outKit}`);
}

function importSample(s: Session, args: string[]): void {
  const [romPath, kitStr, audio] = positionals(args);
  if (!romPath || kitStr == null || !audio) throw new Error("usage: bliptoaster-rom import-sample <rom> <kit> <audio> [--slot N] [--name X] [--out rom] [flags]");
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
  if (!romPath || kitStr == null || slotStr == null) throw new Error("usage: bliptoaster-rom remove-sample <rom> <kit> <slot> [--out rom]");
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

// Validate a theme index against the table the ROM actually carries (16 on every shipped build). BlipToasterRom
// bounds both accessors itself — getTheme returns null, setTheme no-ops — so without this an out-of-range
// import-theme would report success and write an unchanged ROM.
function themeIndexInRange(rom: BlipToasterRom, idx: number): number {
  const count = rom.themeCount;
  if (count === 0) throw new Error("no theme table in this ROM");
  if (!Number.isInteger(idx) || idx < 0 || idx >= count) throw new Error(`theme index ${idx} out of range (0..${count - 1})`);
  return idx;
}

function exportTheme(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = positionals(args);
  if (!romPath || idxStr == null || !out) throw new Error("usage: bliptoaster-rom export-theme <rom> <index> <out.rit>");
  const rom = openRom(s, romPath);
  const t = rom.getTheme(themeIndexInRange(rom, parseInt(idxStr, 10)))!;
  const theme = decodeThemeFromRom(t.recordBytes, t.nameBytes);
  if (!s.backend.writeFile(out, enc.encode(JSON.stringify(serializeRit(theme), null, 2) + "\n"))) throw new Error(`write failed: ${out}`);
  console.log(`wrote theme ${idxStr} to ${out}`);
}

function importTheme(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = positionals(args);
  if (!romPath || !file || idxStr == null) throw new Error("usage: bliptoaster-rom import-theme <rom> <in.rit> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const idx = themeIndexInRange(rom, parseInt(idxStr, 10));
  const { theme } = parseRit(JSON.parse(dec.decode(readOrThrow(s, file, "theme .rit")))); // throws on a bad .rit
  const t = normalizeTheme(theme);
  rom.setTheme(idx, encodeThemeRecord(t), encodeThemeName(t));
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into theme ${idxStr}; wrote ${out}`);
}

function exportFont(s: Session, args: string[]): void {
  const [romPath, idxStr, out] = positionals(args);
  if (!romPath || idxStr == null || !out) throw new Error("usage: bliptoaster-rom export-font <rom> <index> <out.chr>");
  const rom = openRom(s, romPath);
  const bank = rom.getChrFontSlot(parseInt(idxStr, 10));
  if (!bank) throw new Error(`font ${idxStr} out of range / no CHR region`);
  if (!s.backend.writeFile(out, bank)) throw new Error(`write failed: ${out}`);
  console.log(`wrote font ${idxStr} (8 KB CHR bank) to ${out}`);
}

function importFont(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = positionals(args);
  if (!romPath || !file || idxStr == null) throw new Error("usage: bliptoaster-rom import-font <rom> <in.chr> <index> [--out rom]");
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
  if (!romPath || idxStr == null || !out) throw new Error("usage: bliptoaster-rom export-kit <rom> <index> <out.rkit>");
  const rom = openRom(s, romPath);
  const bank = rom.getKitBank(parseInt(idxStr, 10));
  if (!bank || !isBankPopulated(bank)) throw new Error(`kit ${idxStr} is empty / out of range`);
  if (!s.backend.writeFile(out, bank)) throw new Error(`write failed: ${out}`);
  console.log(`wrote kit ${idxStr} (8 KB DPCM bank) to ${out}`);
}

function importKit(s: Session, args: string[]): void {
  const [romPath, file, idxStr] = positionals(args);
  if (!romPath || !file || idxStr == null) throw new Error("usage: bliptoaster-rom import-kit <rom> <in.rkit> <index> [--out rom]");
  const rom = openRom(s, romPath);
  const idx = kitIndexInRange(rom, parseInt(idxStr, 10));
  const data = readOrThrow(s, file, "kit .rkit");
  if (data.length !== KIT_BANK_SIZE || !isBankPopulated(data)) throw new Error(`.rkit must be a populated 8 KB DPCM bank`);
  rom.setKit(idx, data); // plain bank splice — BlipToaster reads the kit index directly at boot (no mirror)
  const out = flag(args, "--out") ?? romPath;
  if (!s.backend.writeFileAtomic(out, rom.bytes())) throw new Error(`write failed: ${out}`);
  console.log(`imported ${file} into kit ${idx}; wrote ${out}`);
}

function patchManifest(s: Session, args: string[]): void {
  const [romPath, manifestPath, outRom] = positionals(args);
  if (!romPath || !manifestPath || !outRom) throw new Error("usage: bliptoaster-rom patch <rom> <manifest.json> <out>");
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
    const idx = themeIndexInRange(rom, te.slot);
    const { theme } = parseRit(JSON.parse(dec.decode(readOrThrow(s, resolvePath(manifestPath, te.file), "theme"))));
    const t = normalizeTheme(theme);
    rom.setTheme(idx, encodeThemeRecord(t), encodeThemeName(t));
    applied++;
  }

  for (const fe of m.fonts ?? []) {
    if (!fe.file) throw new Error(`font entry (slot ${fe.slot}) needs "file"`);
    const data = readOrThrow(s, resolvePath(manifestPath, fe.file), "font");
    if (data.length !== CHR_BANK_SIZE) throw new Error(`font ${fe.file}: .chr must be exactly 8 KB`);
    rom.setChrFontSlot(fe.slot, data);
    applied++;
  }

  if (m.settings) {
    if (!rom.hasSettings) throw new Error('manifest has "settings" but this ROM carries no readable settings block');
    // A manifest written against the old key would otherwise apply CLEANLY and do nothing at all (setSettings
    // writes named fields only), leaving the screen byte at whatever it was. Name the replacement and the
    // inversion rather than let that pass.
    if ("mode1" in (m.settings as Record<string, unknown>))
      throw new Error('manifest "settings" uses "mode1", which is now "ppu" with the opposite sense: "mode1": true becomes "ppu": false');
    rom.setSettings(m.settings);
    applied++;
  }

  if (!s.backend.writeFileAtomic(outRom, rom.bytes())) throw new Error(`write failed: ${outRom}`);
  console.log(`applied ${applied} manifest entr${applied === 1 ? "y" : "ies"}; wrote ${outRom}`);
}

const BLIPTOASTER_ROM_HELP = [
  "usage: retroplug-cli bliptoaster-rom <subcommand> ...",
  "",
  "  info          <rom> [--json]                     theme / font / kit inventory + baked settings",
  "  settings      <rom> [field flags] [--out <nes>]  print or patch the baked rig settings (no flags = print)",
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
  "settings flags (each optional; only the ones given are written):",
  "  --base-channel 1-16   the cart's base MIDI channel (BASE01..BASE16)",
  "  --kit 0-15            the DMC kit bank ch5 plays out of at boot",
  "  --ppu on|off          draw the screen at boot (off = dark until START, the old Mode 1)",
  "  --mode1 on|off        DEPRECATED alias for the INVERSE of --ppu (the byte's meaning flipped)",
  "  --curve linear|log    the velocity curve on every channel (the VRC7 build has none)",
  "  --theme 0-15          the colour theme the screen comes up in",
  "  --font 0-3            the CHR font the screen comes up in",
  "  Each is a BOOT default, not a lock: the cart's own CCs (14/15/16/17) still move it live, and the next",
  "  cold boot returns to the baked value. RetroPlug's BlipToaster > Settings submenu edits the same block",
  "  non-destructively (pinned in the project, folded into the ROM in memory) instead of writing the file.",
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
  '    "fonts":  [{ "slot": 0, "file": "big.chr" }],',
  '    "settings": { "theme": 11, "font": 2, "kit": 3, "baseChannel": 3, "ppu": true, "velCurve": false } }',
  '  ("settings" is not per-slot - it is the one baked block, and only the fields named are written)',
  "build-kit takes ONE kit entry without a slot, e.g.",
  '  { "name": "MYKIT", "build": [{ "file": "kick.wav", "name": "BD", "rate": 12 }, "snare.wav"] }   → then import-kit',
  "  (build source: a path string, or { file, name?, rate?, loop?, normalize?, offset?, length?, effects? })",
].join("\n");

export const blipToasterRomTool: CliTool = {
  name: "bliptoaster-rom",
  summary: "inspect / extract / edit BlipToaster ROM kits, theme and font (+ compile WAV → .rkit)",
  help: BLIPTOASTER_ROM_HELP,
  run(s: Session, args: string[]): void {
    const sub = args[0];
    const rest = args.slice(1);
    if (sub === "info") return info(s, rest);
    if (sub === "settings") return settings(s, rest);
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
    throw new Error(`unknown subcommand '${sub ?? ""}'\n\n${BLIPTOASTER_ROM_HELP}`);
  },
};
