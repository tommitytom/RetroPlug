// Regression for the Load SRAM fix: writing a system's cartridge battery RAM is
// INERT on the running game — a GB game only re-reads its SRAM at boot, so the
// loaded save isn't used until a reset. This is exactly why the plugin's
// LoadSram command pairs loadSramBytes with onReset.
//
// Asserted deterministically on a single instance (cross-instance work-RAM
// comparison is unusable here: SameBoy fills power-on RAM per-instance, so two
// identical LSDj boots differ in ~4 KB of never-written work RAM). Scope: this
// proves the load/reset *mechanism*; it does not assert the audio becomes the
// new song (that needs START + audio analysis).
import { test, expect, emu, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const SETTLE = 3000; // authored sav skips the self-test -> LSDj is running

function song(tempo: number, note: number): ArrayBuffer {
  return emu.savFromJson(
    JSON.stringify({
      workingSong: {
        formatVersion: 22,
        settings: { tempo, syncMode: "Lsdj" },
        rows: [{ chains: [0] }],
        chains: [{ phrases: [0] }],
        phrases: [{ notes: [note], instruments: [0] }],
        instruments: [{ type: "pulse" }],
      },
    }),
  );
}

function bytesEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

test("loading SRAM is inert on the running game until a reset", () => {
  const savA = song(128, 1);
  const savB = song(90, 40);

  // Boot song A and let LSDj reach a running state.
  const sys = emu.loadRom(LSDJ, savA);
  emu.runMs(SETTLE);
  const wramRunningA = emu.readMemory(sys, Mem.Ram);

  // Overwrite the battery RAM with song B, no frames run in between.
  expect(emu.loadSram(sys, savB)).toBe(true);

  // Cart battery RAM is updated immediately...
  expect(bytesEqual(emu.readMemory(sys, Mem.Sram), new Uint8Array(savB))).toBe(true);
  // ...but the running game's work RAM is byte-for-byte untouched — the load
  // didn't perturb the live emulator at all. THIS is why the new song isn't
  // used until a reset.
  expect(bytesEqual(emu.readMemory(sys, Mem.Ram), wramRunningA)).toBe(true);

  // A reset keeps the battery RAM (it survives a power cycle)...
  emu.reset(sys);
  expect(bytesEqual(emu.readMemory(sys, Mem.Sram), new Uint8Array(savB))).toBe(true);

  // ...and reboots the game, wholesale-replacing the running state that the
  // inert load left in place.
  emu.runMs(SETTLE);
  expect(bytesEqual(emu.readMemory(sys, Mem.Ram), wramRunningA)).toBe(false);
});
