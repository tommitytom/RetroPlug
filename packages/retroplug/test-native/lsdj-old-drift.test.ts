// End-to-end READER validation on real OLD-era cores (v5.0.3, v6.9.0) — the versions the new full-layout
// detector (driftLayouts.generated.ts) now covers but the old rigid shift model did not. Proves the whole
// live pipeline works below v8.2.1: identify → resolveLayout (full drift layout) → decode a live WRAM
// snapshot into screen / tempo / per-screen cursor. Boots an authored era-format song, drives it with
// renderAudio only (never startAudio — the single-threaded direct-render regime that keeps readMemory
// live), navigates screens, and asserts the reader tracks the real on-screen state.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { decodeSav, savFrom } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";
import { LsdjReader } from "../src/lsdj/runtime";

declare const __RESOURCES_DIR__: string;
const DIR = __RESOURCES_DIR__ + "/roms/lsdj";
const RIGHT = 0, DOWN = 3, SELECT = 6;
const TEMPO = 150; // 0x96 — distinctive, valid BPM; must read back through the detected TEMPO offset

const pulse = { type: "pulse", panning: "LeftRight" } as const;
function detectionSong(fmt: number): Uint8Array {
  return savFrom({
    workingSong: {
      formatVersion: fmt, settings: { syncMode: "None", tempo: TEMPO },
      rows: [{ chains: [0, 1, 2, 3] }],
      chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] }],
      phrases: [{ notes: [1], instruments: [0] }, { notes: [1], instruments: [1] }, { notes: [1], instruments: [2] }, { notes: [1], instruments: [3] }],
      instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
    },
  });
}

for (const slug of ["lsdj5_0_3", "lsdj6_9_0"]) {
  test(`old-drift reader: ${slug} decodes live screen/tempo/cursor from the full drift layout`, () => {
    const be = createRealBackend();
    const ROM = `${DIR}/${slug}.gb`, SAV = `${DIR}/${slug}.sav`;
    if (!be.fileExists(ROM) || !be.fileExists(SAV)) { console.log(`# SKIP ${slug}: ROM/sav not found`); return; }

    const reader = LsdjReader.fromHeader(be.readFilePrefix(ROM, 0x150)!);
    expect(reader.supported).toBeTruthy();
    // The full-layout detector must have covered this old version: screen + tempo + all five cursors.
    const L = reader.layout!;
    expect(L.currentScreen != null).toBeTruthy();
    expect(L.tempo != null).toBeTruthy();
    for (const s of ["song", "chain", "phrase", "instrument", "table"] as const) expect(L.cursors![s]).toBeTruthy();

    const audio = createAudioDriver();
    const id = 1;
    const fmt = decodeSav(be.readFilePrefix(SAV, 0x20000)!).workingSong.formatVersion;
    expect(be.constructSystem({ romPath: ROM, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: detectionSong(fmt) }, id)).toBeTruthy();
    const decode = () => reader.read(be.readMemory(id, MemoryRegion.Ram)!);
    const chord = (dir: number) => { audio.pressButton(id, SELECT, true); audio.renderAudio(200); audio.pressButton(id, dir, true); audio.renderAudio(90); audio.pressButton(id, dir, false); audio.renderAudio(40); audio.pressButton(id, SELECT, false); audio.renderAudio(150); };

    audio.renderAudio(6000); // reach SONG
    const onSong = decode();
    console.log(`[${slug}] on SONG: screen=${onSong.screen} tempo=${onSong.tempo} cursor=${JSON.stringify(onSong.cursor)}`);
    expect(onSong.screen).toBe("song");
    expect(onSong.tempo).toBe(TEMPO); // the authored BPM read back through the detected TEMPO offset
    expect(onSong.cursor).toBeTruthy();

    // Navigate SONG → CHAIN: the reader must report the new active screen AND its cursor.
    chord(RIGHT);
    const onChain = decode();
    console.log(`[${slug}] on CHAIN: screen=${onChain.screen} cursor=${JSON.stringify(onChain.cursor)}`);
    expect(onChain.screen).toBe("chain");

    // Move the cursor down; the reader must track the row on the active screen.
    const before = decode().cursor!;
    audio.pressButton(id, DOWN, true); audio.renderAudio(70); audio.pressButton(id, DOWN, false); audio.renderAudio(70);
    const after = decode().cursor!;
    console.log(`[${slug}] CHAIN cursor row ${before.row} → ${after.row}`);
    expect(after.row).toBe(before.row + 1);

    be.removeSystem(id);
  });
}
