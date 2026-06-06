// Generate a fresh, SRAM-initialised .sav for every LSDj ROM, in parallel.
//
// For each `<dir>/*.gb` it runs `retroplug-cli` with a tiny no-wav script that
// just boots the ROM and advances emulated time long enough for LSDj's
// cartridge/SRAM self-test to finish, then dumps system 0's battery RAM (via
// the `--save-sav` flag) to `<outdir>/<romstem>.sav`. Runs a worker pool sized
// to the core count. Each produced sav is validated (128 KiB, `jk` init magic,
// plausible format-version byte) and a format-version histogram is printed.
//
// Build first:  cmake --build build --target retroplug-cli -j$(nproc)
// Run:          node tools/build-test-ish ... (transpiled via esbuild; see header)
//
// Env overrides: ROMS_DIR, OUT_DIR, DURATION_MS, CONCURRENCY.
import { spawn } from "node:child_process";
import { readdirSync, readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { cpus } from "node:os";
import * as path from "node:path";

// Run from the repo root (the transpiled bundle may live elsewhere, so don't
// rely on import.meta.url). Override with REPO=... if needed.
const REPO = process.env.REPO ?? process.cwd();
const ROMS_DIR = process.env.ROMS_DIR ?? path.resolve(REPO, "../resources/roms/lsdj");
const OUT_DIR = process.env.OUT_DIR ?? ROMS_DIR;
const DURATION_MS = Number(process.env.DURATION_MS ?? 25000);
const RETRY_DURATION_MS = DURATION_MS * 2;
const CONCURRENCY = Number(process.env.CONCURRENCY ?? cpus().length);
const CLI = path.resolve(REPO, "build/bin/retroplug-cli");

const SAV_SIZE = 0x20000; // 128 KiB
const JK_OFF = 0x813e;
const FMT_OFF = 0x7fff;

type Result = {
  rom: string;
  sav: string;
  ok: boolean;
  fmt?: number;
  kind?: Kind;
  reason?: string;
  retried?: boolean;
};

function writeScript(durationMs: number): string {
  const p = path.join(OUT_DIR, `.sram_init_${durationMs}.json`);
  writeFileSync(p, JSON.stringify({ duration_ms: durationMs, sample_rate: 44100, block_size: 1024, events: [] }));
  return p;
}

function runCli(romPath: string, savPath: string, scriptPath: string): Promise<{ code: number; stderr: string }> {
  return new Promise((resolve) => {
    const child = spawn(CLI, ["--script", scriptPath, "--rom", romPath, "--save-sav", savPath], {
      stdio: ["ignore", "ignore", "pipe"],
    });
    let stderr = "";
    child.stderr.on("data", (d) => (stderr += d.toString()));
    child.on("error", (e) => resolve({ code: -1, stderr: String(e) }));
    child.on("close", (code) => resolve({ code: code ?? -1, stderr }));
  });
}

// Validate a produced sav, era-aware. LSDj's SRAM layout changed across its
// life: modern builds (~3.6+) carry the working-song format byte at 0x7FFF and
// the `jk` init magic at 0x813E; older builds initialise a valid sav with no
// format byte there (empty working song), and the earliest used a 32 KiB SRAM.
// So "valid" = the self-test actually initialised the SRAM (not all-0xFF, not
// empty) — NOT "matches the modern layout".
type Kind = "modern" | "early" | "early32k" | "uninitialised";
function validate(savPath: string): { ok: boolean; fmt?: number; kind: Kind; reason?: string } {
  if (!existsSync(savPath)) return { ok: false, kind: "uninitialised", reason: "no file" };
  const b = readFileSync(savPath);
  let ff = 0, nz = 0;
  for (const x of b) { if (x === 0xff) ff++; if (x !== 0) nz++; }
  const frac = nz / b.length;
  if (ff === b.length || frac < 0.05)
    return { ok: false, kind: "uninitialised", reason: ff === b.length ? "all 0xFF (self-test failed/never ran)" : `near-empty (${(frac * 100) | 0}% nonzero)` };
  const jk = b.indexOf(Buffer.from("jk")) >= 0;
  const fmt = b.length > FMT_OFF ? b[FMT_OFF] : undefined;
  if (b.length === 32768) return { ok: true, kind: "early32k", fmt };
  if (jk && fmt !== 0 && fmt !== 0xff) return { ok: true, kind: "modern", fmt };
  if (jk || frac > 0.3) return { ok: true, kind: "early", fmt }; // valid early-era sav
  return { ok: false, kind: "uninitialised", reason: `no 'jk', ${(frac * 100) | 0}% nonzero` };
}

async function processOne(romPath: string, scriptMain: string, scriptRetry: string): Promise<Result> {
  const stem = path.basename(romPath, ".gb");
  const sav = path.join(OUT_DIR, `${stem}.sav`);
  let res = await runCli(romPath, sav, scriptMain);
  let v = res.code === 0 ? validate(sav) : { ok: false, reason: `cli exit ${res.code}` };
  // One retry at a longer duration for ROMs whose self-test didn't finish in time.
  if (!v.ok) {
    res = await runCli(romPath, sav, scriptRetry);
    const v2 = res.code === 0 ? validate(sav) : { ok: false, reason: `cli exit ${res.code}` };
    return { rom: stem, sav, ...v2, retried: true };
  }
  return { rom: stem, sav, ...v };
}

async function main() {
  if (!existsSync(CLI)) {
    console.error(`retroplug-cli not found at ${CLI} — build it first:\n  cmake --build build --target retroplug-cli -j$(nproc)`);
    process.exit(2);
  }
  mkdirSync(OUT_DIR, { recursive: true });
  const roms = readdirSync(ROMS_DIR)
    .filter((f) => f.endsWith(".gb"))
    .map((f) => path.join(ROMS_DIR, f))
    .sort();

  console.error(`${roms.length} ROMs | out=${OUT_DIR} | duration=${DURATION_MS}ms | concurrency=${CONCURRENCY}`);
  const scriptMain = writeScript(DURATION_MS);
  const scriptRetry = writeScript(RETRY_DURATION_MS);
  const t0 = Date.now();

  const results: Result[] = new Array(roms.length);
  let next = 0;
  let done = 0;
  async function worker() {
    while (true) {
      const i = next++;
      if (i >= roms.length) return;
      results[i] = await processOne(roms[i], scriptMain, scriptRetry);
      done++;
      if (done % 25 === 0 || done === roms.length) {
        process.stderr.write(`\r  ${done}/${roms.length} done`);
      }
    }
  }
  await Promise.all(Array.from({ length: Math.min(CONCURRENCY, roms.length) }, worker));
  process.stderr.write("\n");

  const wall = ((Date.now() - t0) / 1000).toFixed(1);
  const ok = results.filter((r) => r.ok);
  const fail = results.filter((r) => !r.ok);

  // Format-version histogram (only meaningful for the modern layout that writes
  // a format byte at 0x7FFF; early-era savs leave it 0).
  const hist = new Map<number, number>();
  for (const r of ok) if (r.kind === "modern" && r.fmt !== undefined) hist.set(r.fmt, (hist.get(r.fmt) ?? 0) + 1);
  const histStr = [...hist.entries()].sort((a, b) => a[0] - b[0]).map(([f, n]) => `fmt${f}:${n}`).join("  ");
  const kinds = new Map<string, number>();
  for (const r of ok) kinds.set(r.kind ?? "?", (kinds.get(r.kind ?? "?") ?? 0) + 1);

  console.error(`\nDONE in ${wall}s — ${ok.length} ok, ${fail.length} failed`);
  console.error(`kinds: ${[...kinds.entries()].map(([k, n]) => `${k}:${n}`).join("  ")}`);
  console.error(`modern format versions: ${histStr}`);
  if (fail.length) {
    console.error(`\nFAILURES (${fail.length}):`);
    for (const r of fail) console.error(`  ${r.rom}: ${r.reason}${r.retried ? " (after retry)" : ""}`);
  }
  process.exit(fail.length ? 1 : 0);
}

main();
