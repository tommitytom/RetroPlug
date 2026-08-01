#!/usr/bin/env node
// LSDj FULL drift-layout generator. Bundles tools-src/lsdj-detect-offsets.ts with esbuild and runs it on
// the retroplug-host binary (real cores + readMemory) over the LSDj ROM corpus, then writes the per-
// version drift layout (CURRENT_SCREEN + TEMPO + all five cursors) to
// src/lsdj/runtime/driftLayouts.generated.ts.
//
//   node scripts/gen-lsdj-offsets.mjs [filterSubstrings] [--limit N] [--dry-run]
//
// filterSubstrings: comma-separated filename substrings (e.g. "9_4_2,6_9_0"); omitted = whole corpus.
// The whole corpus is ~550 ROMs and each ROM takes ~30s (two boots + nav + per-screen cursor probes), so
// the full run is long — filter/batch it, or run unfiltered in the background. Runs MERGE into the
// existing table (a filtered run only updates the keys it covers), so coverage can be built up in passes.
import { existsSync, writeFileSync, readFileSync, mkdtempSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { buildSync } from "esbuild";

const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");
const RESOURCES_DIR = process.env.RETROPLUG_RESOURCES_DIR || resolve(REPO, "../resources");
const CORPUS_DIR = join(RESOURCES_DIR, "roms/lsdj");
const HOST = process.env.RETROPLUG_HOST || join(REPO, "build/bin/retroplug-host" + (process.platform === "win32" ? ".exe" : ""));
const OUT = join(PKG, "src/lsdj/runtime/driftLayouts.generated.ts");
const ENTRY = join(PKG, "tools-src/lsdj-detect-offsets.ts");

const args = process.argv.slice(2);
const dryRun = args.includes("--dry-run");
const limitIdx = args.indexOf("--limit");
const limit = limitIdx >= 0 ? args[limitIdx + 1] : "0";
const filter = args.find((a) => !a.startsWith("--") && a !== limit) || "";

if (!existsSync(HOST)) {
  console.error(`retroplug-host not found: ${HOST}\n  build it:  node scripts/cmake-build.js retroplug-host`);
  process.exit(1);
}
if (!existsSync(CORPUS_DIR)) {
  console.error(`LSDj corpus not found: ${CORPUS_DIR}  (populate with python3 ../resources/download_lsdj.py)`);
  process.exit(1);
}

const outFile = join(mkdtempSync(join(tmpdir(), "rp-gen-")), "detect.js");
buildSync({
  entryPoints: [ENTRY],
  bundle: true,
  format: "esm",
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  outfile: outFile,
  define: {
    "process.env.NODE_ENV": '"production"',
    __CORPUS_DIR__: JSON.stringify(CORPUS_DIR),
    __FILTER__: JSON.stringify(filter),
    __LIMIT__: JSON.stringify(String(limit)),
  },
});

console.log(`[gen] running full-layout detector on ${HOST} …`);
const run = spawnSync(HOST, [outFile], { encoding: "utf8", stdio: ["ignore", "pipe", "inherit"], maxBuffer: 64 * 1024 * 1024 });
rmSync(dirname(outFile), { recursive: true, force: true });
process.stdout.write(run.stdout ?? "");
if (run.status !== 0) {
  console.error(`[gen] detector exited ${run.status}`);
  process.exit(1);
}

const m = /###LSDJ_DETECT_JSON_START###\n([\s\S]*?)\n###LSDJ_DETECT_JSON_END###/.exec(run.stdout ?? "");
if (!m) {
  console.error("[gen] no JSON block in detector output");
  process.exit(1);
}
const results = JSON.parse(m[1]);

// Merge PER FIELD, keeping the richer value. A fresh detection fills in the fields it found; a field it
// FAILED to detect (null tempo / a missing cursor) is a detection failure, not a signal the field is gone,
// so it keeps the previously-known value instead of clobbering it. Without this, a weaker re-detection — a
// partial ROM sharing a version key, or a flaky second pass — would silently regress a fuller entry
// (offsets.ts takes a driftLayouts row wholesale, with no per-field fallback to driftShifts). This also
// makes a clean re-run idempotent even when a field detects flakily.
const CURSOR_ORDER = ["song", "chain", "phrase", "instrument", "table"];
const layouts = readExisting();
let added = 0;
let updated = 0;
const regressions = [];
for (const r of results) {
  if (r.status !== "ok" || r.key == null || r.currentScreen == null) continue;
  const prev = layouts[r.key];
  const cursors = {};
  for (const s of CURSOR_ORDER) {
    const c = r.cursors?.[s] ?? prev?.cursors?.[s]; // prefer freshly-detected, else keep prior; canonical order → stable diffs
    if (c) cursors[s] = c;
  }
  const entry = { currentScreen: r.currentScreen, tempo: r.tempo ?? prev?.tempo ?? null, cursors };

  // Note fields this ROM detected WEAKER than what we already had (we kept the prior value).
  const lost = [];
  if (r.tempo == null && prev?.tempo != null) lost.push("tempo");
  for (const s of CURSOR_ORDER) if (!r.cursors?.[s] && prev?.cursors?.[s]) lost.push(s);
  if (lost.length) regressions.push({ file: r.file, key: r.key, lost });

  if (prev === undefined) added++;
  else if (JSON.stringify(prev) !== JSON.stringify(entry)) updated++;
  layouts[r.key] = entry;
}

const ok = results.filter((r) => r.status === "ok").length;
console.log(`[gen] detected ${ok}/${results.length} ROMs; +${added} new, ~${updated} changed; ${Object.keys(layouts).length} total.`);
for (const r of results.filter((r) => r.status !== "ok" && r.status !== "not-lsdj")) console.log(`[gen]   skip ${r.file}: ${r.status}`);
const partial = results.filter((r) => r.status === "ok" && ["song", "chain", "phrase", "instrument", "table"].some((s) => !r.cursors?.[s]));
for (const r of partial) console.log(`[gen]   partial ${r.file} (${r.key}): missing ${["song", "chain", "phrase", "instrument", "table"].filter((s) => !r.cursors?.[s]).join(",")}`);
for (const g of regressions) console.log(`[gen]   kept prior ${g.key} ${g.lost.join(",")} — this ROM (${g.file}) detected them weaker (no regression)`);

if (dryRun) {
  console.log("[gen] --dry-run: not writing", OUT);
  process.exit(0);
}
writeFileSync(OUT, render(layouts));
console.log(`[gen] wrote ${OUT}`);

// Parse the existing generated table back into an object (hex → number, barewords → JSON) so filtered
// runs merge instead of wiping uncovered keys.
function readExisting() {
  try {
    const src = readFileSync(OUT, "utf8");
    const start = src.indexOf("= {", src.indexOf("driftLayouts"));
    if (start < 0) return {};
    let body = src.slice(src.indexOf("{", start));
    body = body.slice(0, body.lastIndexOf("}") + 1);
    body = body
      .replace(/0x[0-9a-fA-F]+/g, (h) => String(parseInt(h, 16))) // hex → decimal
      .replace(/([{,]\s*)([a-zA-Z_][a-zA-Z0-9_]*)\s*:/g, '$1"$2":') // bareword keys → quoted
      .replace(/,(\s*[}\]])/g, "$1"); // trailing commas
    return JSON.parse(body);
  } catch {
    return {};
  }
}

function hex(n) {
  return "0x" + n.toString(16);
}
function renderCursor(c) {
  return `{ col: ${hex(c.col)}, row: ${hex(c.row)} }`;
}
function renderEntry(l) {
  const order = ["song", "chain", "phrase", "instrument", "table"];
  const cursors = order.filter((s) => l.cursors[s]).map((s) => `${s}: ${renderCursor(l.cursors[s])}`).join(", ");
  const tempo = l.tempo == null ? "null" : hex(l.tempo);
  return `{ currentScreen: ${hex(l.currentScreen)}, tempo: ${tempo}, cursors: { ${cursors} } }`;
}

function render(layouts) {
  const keys = Object.keys(layouts).sort(cmpVersionKey);
  const lines = keys.map((k) => `  ${JSON.stringify(k)}: ${renderEntry(layouts[k])},`).join("\n");
  return `// GENERATED by tools/gen-lsdj-offsets (pnpm lsdj:gen-offsets) — do not edit by hand.
//
// Per-version FULL drifting WRAM layout for LSDj: CURRENT_SCREEN, TEMPO, and each per-screen cursor
// {col,row}, all WRAM-relative (absolute addr - 0xC000). Detected field-by-field on a real core by
// runtime/detect.ts detectDriftLayout (booting an authored era-format song and probing screen navigation,
// tempo via a two-boot differential, and each cursor via cross-axis-independent d-pad presses).
//
// This SUPERSEDES the rigid single-integer driftShifts model: the drifting block is NOT uniform — screen
// and the tight song/chain/phrase/instrument cursor cluster shift together, but TEMPO and the TABLE cursor
// drift independently (e.g. 8.5.1's real tempo is 0xC537, not the shift-predicted 0xC526). A version present
// here resolves its full drift layout exactly; one absent falls back to driftShifts, then to the legacy
// SONG cursor only (see layoutForVersion in offsets.ts).
//
// Key = "major.minor.patchLabel" plus "-build" for forks (e.g. "9.3.3-aboy"). Regenerate over the corpus:
//   pnpm lsdj:gen-offsets            (whole corpus — long)
//   node packages/retroplug/scripts/gen-lsdj-offsets.mjs 6_9_0,8_5_1   (a filtered subset, merged in)
import type { CursorOffset, Screen } from "./types";

export interface DriftLayout {
  currentScreen: number;
  tempo: number | null;
  cursors: Partial<Record<Screen, CursorOffset>>;
}

export const driftLayouts: Record<string, DriftLayout> = {
${lines}
};
`;
}

function cmpVersionKey(a, b) {
  const pa = a.split("."), pb = b.split(".");
  const rank = (s) => (/^\d+$/.test(s) ? parseInt(s, 10) : /^[A-Z]$/i.test(s) ? 10 + s.toUpperCase().charCodeAt(0) - 65 : 999);
  for (let i = 0; i < 3; i++) {
    const d = i < 2 ? parseInt(pa[i], 10) - parseInt(pb[i], 10) : rank(pa[2].split("-")[0]) - rank(pb[2].split("-")[0]);
    if (d) return d;
  }
  return String(a).localeCompare(String(b));
}
