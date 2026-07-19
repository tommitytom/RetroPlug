// Validates detectDriftLayout() — the full per-field drift detector that feeds driftLayouts.generated.ts
// — on real cores across every era (v4.3.0 → v9.4.2). Drives the pure detector (src/lsdj/runtime/detect.ts)
// through a DetectDriver wired to the real backend + audio, booting an authored era-format song at two
// distinct tempos (the TEMPO differential). Three independent cross-checks pin the result down:
//   * the detected SONG cursor equals the ported legacy table (offsets.ts invariant #1);
//   * on versions the scalar detector covers (driftShifts, v8.2.1+), CURRENT_SCREEN and the tight cursor
//     cluster (song/chain/phrase/instrument) equal REF_DRIFT + shift — the rigid part of the block;
//   * TEMPO and the TABLE cursor equal independently-probed ground truth — they DON'T follow the shift
//     (e.g. 8.5.1's real tempo is 0xC537, not the shift-predicted 0xC526), which is exactly why the full
//     per-field layout supersedes the rigid model.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { decodeSav, savFrom } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";
import { detectDriftLayout } from "../src/lsdj/runtime/detect";
import type { DetectDriver } from "../src/lsdj/runtime/detect";
import { legacyOffsets } from "../src/lsdj/runtime/legacyOffsets.generated";
import { driftShifts } from "../src/lsdj/runtime/driftShifts.generated";

declare const __RESOURCES_DIR__: string;
const DIR = __RESOURCES_DIR__ + "/roms/lsdj";
const TARGETS = ["lsdj4_3_0", "lsdj5_0_3", "lsdj6_9_0", "lsdj8_5_1", "lsdj9_4_2"];
const TEMPO_A = 190, TEMPO_B = 120; // two distinctive tempos for the TEMPO differential
// Ground truth for the NON-rigid fields, verified independently via the boot-diff / table probes. The
// rigid driftShifts model is WRONG for these (8.5.1 tempo 0x526 and table 0x929 are static; 9.4.2 table
// 0x92c is static) — the per-field detector finds the real moving register, which these assert.
const GROUND: Record<string, { tempo: number; table: { col: number; row: number } }> = {
  "8.5.1": { tempo: 0x537, table: { col: 0x969, row: 0x96a } },
  "9.4.2": { tempo: 0x529, table: { col: 0x92f, row: 0x930 } },
};
const hex = (n: number) => "0x" + n.toString(16);

// The 9.2.L reference the scalar driftShifts apply to (mirror of offsets.ts REF_DRIFT).
const REF = {
  tempo: 0x52a, currentScreen: 0x402,
  cursors: { song: { col: 0x41e, row: 0x41f }, chain: { col: 0x41a, row: 0x41b }, phrase: { col: 0x416, row: 0x417 }, instrument: { col: 0x429, row: 0x428 }, table: { col: 0x92d, row: 0x92e } },
} as const;

// The era format version = the companion sav's working-song format (fmt 2..22 across v4.3..v9.4).
function eraFormat(companion: Uint8Array): number {
  return decodeSav(companion).workingSong.formatVersion;
}
// A 4-channel detection song authored at a given format version, tempo distinctive. Authored (not the
// companion sav) because a companion sav leaves some builds — e.g. 8.5.1 — in a state where SELECT+RIGHT
// nav doesn't register; an era-format authored song boots to SONG and navigates on every era.
const pulse = { type: "pulse", panning: "LeftRight" } as const;
function detectionSong(fmt: number, tempo: number): Uint8Array {
  return savFrom({
    workingSong: {
      formatVersion: fmt, settings: { syncMode: "None", tempo },
      rows: [{ chains: [0, 1, 2, 3] }],
      chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] }],
      phrases: [{ notes: [1], instruments: [0] }, { notes: [1], instruments: [1] }, { notes: [1], instruments: [2] }, { notes: [1], instruments: [3] }],
      instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
    },
  });
}

test("detectDriftLayout: full screen/tempo/cursor layout on real cores; cross-checks legacy + driftShifts", () => {
  const be = createRealBackend();
  const present = TARGETS.filter((t) => be.fileExists(`${DIR}/${t}.gb`) && be.fileExists(`${DIR}/${t}.sav`));
  if (present.length === 0) { console.log(`# SKIP: no target ROM/sav under ${DIR}`); return; }
  const audio = createAudioDriver();
  const driver: DetectDriver = {
    readFilePrefix: (p, n) => be.readFilePrefix(p, n),
    construct: (id, romPath, sram) => be.constructSystem({ romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: sram }, id),
    remove: (id) => { be.removeSystem(id); },
    readWram: (id) => be.readMemory(id, MemoryRegion.Ram),
    press: (id, button, down) => audio.pressButton(id, button, down),
    render: (ms) => audio.renderAudio(ms),
  };

  for (const slug of present) {
    const ROM = `${DIR}/${slug}.gb`;
    const fmt = eraFormat(be.readFilePrefix(`${DIR}/${slug}.sav`, 0x20000)!);
    const r = detectDriftLayout(driver, ROM, detectionSong(fmt, TEMPO_A), TEMPO_A, detectionSong(fmt, TEMPO_B), TEMPO_B);
    const cur = (s: keyof typeof r.cursors) => (r.cursors[s] ? `${hex(r.cursors[s]!.col)}/${hex(r.cursors[s]!.row)}` : "—");
    console.log(`\n[${slug}] status=${r.status} key=${r.key} screen=${r.currentScreen != null ? hex(r.currentScreen) : "—"} tempo=${r.tempo != null ? hex(r.tempo) : "—"}`);
    console.log(`  cursors song=${cur("song")} chain=${cur("chain")} phrase=${cur("phrase")} instrument=${cur("instrument")} table=${cur("table")}`);

    expect(r.status).toBe("ok");
    expect(r.currentScreen != null).toBeTruthy();
    expect(r.tempo != null).toBeTruthy();

    // Cross-check #1: SONG cursor == ported legacy table (col/row at legacy indices 4/5).
    const legacy = r.key ? legacyOffsets[r.key] : undefined;
    if (legacy) expect(r.cursors.song).toEqual({ col: legacy[4], row: legacy[5] });

    // Cross-check #2: on driftShifts-covered versions, CURRENT_SCREEN and the TIGHT cursor cluster
    // (song/chain/phrase/instrument) shift RIGIDLY — assert == REF_DRIFT + shift. TABLE and TEMPO sit
    // outside that cluster and do NOT shift rigidly (proven), so they're asserted against ground truth.
    const shift = r.key ? driftShifts[r.key] : undefined;
    if (shift !== undefined) {
      console.log(`  [xcheck] driftShift=${shift}: cluster == REF+shift; table/tempo == ground truth`);
      expect(r.currentScreen).toBe(REF.currentScreen + shift);
      for (const k of ["song", "chain", "phrase", "instrument"] as const) {
        const ref = REF.cursors[k];
        expect(r.cursors[k]).toEqual({ col: ref.col + shift, row: ref.row + shift });
      }
    }
    const g = r.key ? GROUND[r.key] : undefined;
    if (g) {
      expect(r.tempo).toBe(g.tempo);
      expect(r.cursors.table).toEqual(g.table);
    }
  }
});
