// Generate packages/retroplug/src/smsggdj/runtime/symbols.generated.ts from a WLA-DX symbol file.
//
// smsggdj is WLA-DX/Z80, so the label file comes from the LINKER rather than the assembler, and the
// Makefile doesn't ask for one. Produce it in the smsggdj tree after a normal `make`:
//
//   wlalink -S build/linkfile build/smsggdj.sms      # writes build/smsggdj.sym
//
// then, here:
//
//   SMSGGDJ_SYM=/workspaces/smsggdj/build/smsggdj.sym SMSGGDJ_VERSION=0.45 \
//     node packages/retroplug/scripts/gen-smsggdj-symbols.mjs
//
// Nothing in the smsggdj repo changes - the flag is passed at generation time rather than baked into
// its Makefile, so this works against any checkout or tag without a patch.
//
// The run MERGES: the named version is added or replaced and every other version already in the file is
// kept verbatim, so a new smsggdj release is one run, not a rewrite.
//
// IMPORTANT, and the same trap gen-risa-symbols.mjs documents: a LOCAL build is not byte-identical to
// the shipped ROM (v0.45 built here hashes 04696fa0…, the vendored binary 3af4a0d1…), so these
// addresses are a claim about a build, not proof about the binary users run. They must then be
// certified against the SHIPPED ROM on a real core - see test-native/sms-layout.test.ts, which writes
// echo_mode and asserts the AUDIO changes. Wrong addresses fail there.
import { readFileSync, writeFileSync, existsSync } from "node:fs";

const SYM = process.env.SMSGGDJ_SYM || "/workspaces/smsggdj/build/smsggdj.sym";
const OUT = new URL("../src/smsggdj/runtime/symbols.generated.ts", import.meta.url);

// The RAM labels a host-side load needs. wave_ram is the working-song block base; song_name and
// echo_mode are the per-song metadata SMDJ4 keeps in the DIRECTORY ENTRY rather than the block
// (src/rle.asm:34 "metadata, not in the block"), which is exactly why the layout is needed at all.
// song_edited is the cart's own dirty flag; prj_slot is which slot PROJECT is pointing at.
const WANT = [
  "wave_ram", "phrase_pool", "song_name", "echo_mode", "song_edited", "prj_slot",
  // Engine state, for the parts of the cart's own `load_rebase` a host-side load has to reproduce when
  // it lands WHILE THE TRANSPORT IS RUNNING. load_rebase `ret z`s on play_state, so all of this is
  // inert for a load made while stopped - which is the common case and needs none of it.
  "play_state", "eng_len", "live_q", "groove_sel", "groove_pos",
  // PSG shadow attenuations (0 loud .. $F silent), one per channel. Not needed to LOAD a song -
  // they are here so a test can certify the echo address by what the engine actually drives,
  // rather than by how loud the mix happens to be in a given window.
  "psg_vols",
];

// The echo settings are EIGHT separate `db`s that the ROM copies as one run
// (`ld hl, echo_mode / ld bc, 8 / ldir` in rle_song_save), and SMDJ4 stores them as one 8-byte field in
// the directory entry. `_sizeof_echo_mode` is therefore 1, not 8 - so the run length is derived from
// the labels and their contiguity is ASSERTED, rather than an 8 being hard-coded here and silently
// becoming wrong if a build ever reorders them.
const ECHO_RUN = ["echo_mode", "echo_tap1", "echo_tap2", "echo_red1", "echo_red2", "echo_stereo", "echo_tsp1", "echo_tsp2"];

/** WLA symbol file: a `[labels]` section of `<bank>:<addr> <name>`, then `[definitions]` carrying the
 *  `_sizeof_*` values as plain 32-bit numbers. Both are parsed - the sizes are what stop a caller from
 *  hard-coding "8 bytes" for a field the ROM might widen. */
function parseSym(path) {
  const labels = {};
  const sizes = {};
  for (const line of readFileSync(path, "utf8").split("\n")) {
    const l = line.match(/^([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+([A-Za-z_][A-Za-z0-9_]*)\s*$/);
    if (l) {
      labels[l[3]] = parseInt(l[2], 16);
      continue;
    }
    const d = line.match(/^([0-9A-Fa-f]{8})\s+_sizeof_([A-Za-z_][A-Za-z0-9_]*)\s*$/);
    if (d) sizes[d[2]] = parseInt(d[1], 16);
  }
  return { labels, sizes };
}

if (!existsSync(SYM)) {
  console.error(`no symbol file at ${SYM}\n  build smsggdj, then:  wlalink -S build/linkfile build/smsggdj.sms`);
  process.exit(1);
}
const { labels, sizes } = parseSym(SYM);

const version =
  process.env.SMSGGDJ_VERSION ||
  (() => {
    // Fall back to the splash string in the source tree next to the .sym, e.g. `.db "V0.45", 0`.
    const main = SYM.replace(/build\/.*$/, "src/main.asm");
    if (!existsSync(main)) return "unknown";
    return readFileSync(main, "utf8").match(/str_version:\s*\.db\s+"V([0-9.]+[a-z]?)"/)?.[1] ?? "unknown";
  })();
if (version === "unknown") throw new Error("could not determine the smsggdj version; pass SMSGGDJ_VERSION");

// Work RAM is mapped at CPU $C000 and readRam/writeRam index the REGION, so the table stores offsets.
// Storing CPU addresses would make every caller subtract, and one of them would eventually forget.
const WRAM_BASE = 0xc000;
const WRAM_LEN = 0x2000;

const out = {};
for (const name of WANT) {
  const addr = labels[name];
  if (addr === undefined) throw new Error(`symbol ${name} not in ${SYM} (is this an smsggdj build?)`);
  if (addr < WRAM_BASE || addr >= WRAM_BASE + WRAM_LEN) {
    throw new Error(`symbol ${name} at $${addr.toString(16)} is outside work RAM $C000-$DFFF - layout changed, revisit`);
  }
  out[name] = addr - WRAM_BASE;
  const size = sizes[name];
  if (size !== undefined) out[`${name}_len`] = size;
}
// wave_ram leads the contiguous 6,912-byte song block, so it must sit at the very base of work RAM for
// "a save-block offset IS a work-RAM offset" to hold. That assumption is load-bearing for every write.
if (out.wave_ram !== 0) throw new Error(`wave_ram is at +$${out.wave_ram.toString(16)}, not the base of work RAM`);

// Derive the echo run length by walking the labels, refusing anything but one contiguous ascending run.
for (let i = 0; i < ECHO_RUN.length; i++) {
  const addr = labels[ECHO_RUN[i]];
  if (addr === undefined) throw new Error(`echo symbol ${ECHO_RUN[i]} not in ${SYM}`);
  if (addr !== labels[ECHO_RUN[0]] + i) {
    throw new Error(
      `echo settings are not contiguous: ${ECHO_RUN[i]} at $${addr.toString(16)}, expected ` +
        `$${(labels[ECHO_RUN[0]] + i).toString(16)}. The ROM copies them as one 8-byte run - revisit.`,
    );
  }
}
out.echo_len = ECHO_RUN.length;

function existingVersions() {
  if (!existsSync(OUT)) return {};
  const prev = {};
  const text = readFileSync(OUT, "utf8");
  for (const block of text.matchAll(/"(\d+\.\d+[a-z]?)": \{([^}]*)\}/g)) {
    const fields = {};
    for (const f of block[2].matchAll(/(\w+): (0x[0-9a-f]+)/g)) fields[f[1]] = parseInt(f[2], 16);
    prev[block[1]] = fields;
  }
  return prev;
}

const table = existingVersions();
const replaced = version in table;
table[version] = out;
const versions = Object.keys(table).sort((a, b) => {
  const [x, y] = [a.match(/\d+/g).map(Number), b.match(/\d+/g).map(Number)];
  return y[0] - x[0] || y[1] - x[1] || b.localeCompare(a);
});

const FIELDS = [...WANT.flatMap((n) => (out[`${n}_len`] !== undefined ? [n, `${n}_len`] : [n])), "echo_len"];

let ts = `// GENERATED by scripts/gen-smsggdj-symbols.mjs - do not hand-edit. Work-RAM OFFSETS (not CPU
// addresses: work RAM is mapped at $C000 and readRam/writeRam index the region) of the smsggdj
// variables a host-side song load has to touch.
//
// Why this exists: SMDJ4 keeps a song's NAME and ECHO settings in the directory entry, not in the
// 6,912-byte block (src/rle.asm:34, "metadata, not in the block"), so poking only the block loads the
// right notes with the previous song's echo - which is audible. These offsets let a load be complete.
//
// One entry per smsggdj version. WLA assigns RAM addresses from RAMSECTION ordering, so adding a
// variable anywhere earlier shifts everything after it; addresses are never assumed to carry across
// versions without being checked. See VERSION_ALIASES in ./layout.ts for versions proven identical.

export interface SmsggdjSymbols {
`;
for (const f of FIELDS) ts += `  ${f}: number;\n`;
ts += `}\n\nexport const SMSGGDJ_SYMBOLS: Record<string, SmsggdjSymbols> = {\n`;
for (const v of versions) {
  ts += `  "${v}": {\n`;
  for (const f of FIELDS) {
    const val = table[v][f];
    if (val === undefined) throw new Error(`existing version ${v} is missing ${f}; regenerate it too`);
    ts += `    ${f}: 0x${val.toString(16)},\n`;
  }
  ts += `  },\n`;
}
ts += `};\n`;

writeFileSync(OUT, ts);
console.log(
  `${replaced ? "replaced" : "added"} smsggdj ${version} in symbols.generated.ts ` +
    `(${FIELDS.length} fields from ${SYM}); file now covers ${versions.join(", ")}`,
);
