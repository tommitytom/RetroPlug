// Host-run entry for the LSDj FULL drift-layout detector. Bundled by scripts/gen-lsdj-offsets.mjs and
// executed on the retroplug-host binary (real SameBoy cores + readMemory). Loops the LSDj ROM corpus and,
// for each identifiable build, detects the complete drifting layout — CURRENT_SCREEN, TEMPO, and all five
// per-screen cursors — by driving the real core (see runtime/detect.ts detectDriftLayout). Prints the
// results as a delimited JSON block on stdout; the Node runner writes driftLayouts.generated.ts.
//
// Per ROM it authors two 4-channel detection songs at the ROM's ERA format version (the companion sav's
// format byte at 0x7FFF) carrying two distinct tempos — an authored song boots to SONG and navigates on
// every era (a companion sav does not on some builds), and the two tempos drive the TEMPO differential.
//
// NOT a test: it does no assertions; it produces data. Drive with `renderAudio` only (never startAudio)
// so the live-core readMemory stays valid (the single-threaded direct-render regime).
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";
import { detectDriftLayout, type DetectDriver, type DriftLayoutResult } from "../src/lsdj/runtime/detect";

declare const __CORPUS_DIR__: string;
declare const __FILTER__: string; // comma-separated filename substrings; "" = all
declare const __LIMIT__: string; // max ROMs to process; "0" = no limit

const MARKER_START = "###LSDJ_DETECT_JSON_START###";
const MARKER_END = "###LSDJ_DETECT_JSON_END###";
const TEMPO_A = 190; // 0xBE
const TEMPO_B = 120; // 0x78 — two distinctive tempos for the differential

// A 4-channel free-running detection song authored at `fmt`, carrying `tempo`. Every ARE_CHANNELS_PLAYING
// flips on START; each screen has content so its cursor moves.
const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;
function detectionSong(fmt: number, tempo: number): Uint8Array {
  return savFrom(
    {
      workingSong: {
        formatVersion: fmt,
        settings: { syncMode: "None", tempo },
        rows: [{ chains: [0, 1, 2, 3] }],
        chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] }],
        phrases: [
          { notes: [1], instruments: [0] },
          { notes: [1], instruments: [1] },
          { notes: [1], instruments: [2] },
          { notes: [1], instruments: [3] },
        ],
        instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
      },
    },
  );
}

const be = createRealBackend();
const audio = createAudioDriver();
const driver: DetectDriver = {
  readFilePrefix: (p, n) => be.readFilePrefix(p, n),
  construct: (id, romPath, sram) =>
    be.constructSystem({ romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: sram }, id),
  remove: (id) => void be.removeSystem(id),
  readWram: (id) => be.readMemory(id, MemoryRegion.Ram),
  press: (id, button, down) => audio.pressButton(id, button, down),
  render: (ms) => void audio.renderAudio(ms),
};

// The ROM's era sav-format version = the companion sav's working-song format byte (0x7FFF). Falls back to
// the modern format when there's no companion sav.
function eraFormat(romPath: string): number {
  const savPath = romPath.replace(/\.gb$/i, ".sav");
  const head = be.readFilePrefix(savPath, 0x8000);
  return head && head.length > 0x7fff ? head[0x7fff] : 22;
}

const corpus = __CORPUS_DIR__;
const filters = __FILTER__.split(",").map((s) => s.trim().toLowerCase()).filter(Boolean);
const limit = parseInt(__LIMIT__, 10) || 0;

let roms = be
  .listDir(corpus)
  .filter((f) => f.toLowerCase().endsWith(".gb"))
  .filter((f) => filters.length === 0 || filters.some((sub) => f.toLowerCase().includes(sub)))
  .sort();
if (limit > 0) roms = roms.slice(0, limit);

console.log(`[detect] corpus=${corpus} matched=${roms.length}${filters.length ? ` (filters: ${filters.join(",")})` : ""}`);

const results: DriftLayoutResult[] = [];
for (const file of roms) {
  const romPath = `${corpus}/${file}`;
  const fmt = eraFormat(romPath);
  let r: DriftLayoutResult;
  try {
    r = detectDriftLayout(driver, romPath, detectionSong(fmt, TEMPO_A), TEMPO_A, detectionSong(fmt, TEMPO_B), TEMPO_B);
  } catch (e) {
    console.log(`[detect] ${file}: ERROR ${(e as Error)?.message ?? e}`);
    continue;
  }
  const c = r.cursors;
  const cur = (s: keyof typeof c) => (c[s] ? `${c[s]!.col.toString(16)}/${c[s]!.row.toString(16)}` : "-");
  console.log(`[detect] ${file}: key=${r.key ?? "?"} fmt=${fmt} scr=${r.currentScreen?.toString(16) ?? "-"} tmp=${r.tempo?.toString(16) ?? "-"} song=${cur("song")} tab=${cur("table")} status=${r.status}`);
  results.push(r);
}

console.log(MARKER_START);
console.log(JSON.stringify(results));
console.log(MARKER_END);

// Exit cleanly so the host reports success (this entry isn't a test).
(globalThis as { tjs?: { exit(n: number): void } }).tjs?.exit(0);
