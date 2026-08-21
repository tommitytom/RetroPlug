// The N8 save-state decoder (src/n8/saveState.ts): a 48 KB NN.SAV -> the full captured game state (CPU regs +
// WRAM + APU/PPU/OAM + CHR + EXRAM). Pure, no hardware. Hand-builds a save-state with known fields at the SST
// offsets (magic, CPU a/x/y/sp, an APU enable, WRAM/CHR/EXRAM markers) and asserts the decode - incl. that the
// 0x1800 block is reused via decodeSniffer.
import { test, expect } from "../../testing/harness";
import { decodeSaveState, SAVESTATE_SIZE } from "../../src/n8/saveState";

function buildSaveState(): Uint8Array {
  const b = new Uint8Array(SAVESTATE_SIZE);
  b[0x18cf] = 0x53; // 'S' magic
  // CPU regs at 0x18C8 (a, x, y, sp) - the values seen on a real .SAV.
  b[0x18c8] = 0x06;
  b[0x18c9] = 0x02;
  b[0x18ca] = 0x00;
  b[0x18cb] = 0xfa;
  b[0x1880 + 0x15] = 0x0f; // APU $4015 (enable) - decodeSniffer reads it at sniffer +0x080+0x15
  b[0x18c0] = 0x88; // PPU ctrl
  b[0x18c1] = 0x1e; // PPU mask (show bg + sprites)
  b[0x0000] = 0xaa; // WRAM marker
  b[0x0010] = 0xbb;
  b[0x2000] = 0xc0; // CHR marker
  b[0x4000] = 0xee; // EXRAM marker
  return b;
}

test("decodeSaveState reaches the CPU regs + WRAM the live sniffer can't", () => {
  const b = buildSaveState();
  const st = decodeSaveState(b);
  expect(st.magicOk).toBe(true);
  expect(st.cpu).toEqual({ a: 0x06, x: 0x02, y: 0x00, sp: 0xfa });
  expect(st.wram.length).toBe(2048);
  expect(st.wram[0]).toBe(0xaa);
  expect(st.wram[0x10]).toBe(0xbb);
  expect(st.vram.length).toBe(4096);
  expect(st.chr.length).toBe(8192);
  expect(st.chr[0]).toBe(0xc0);
  expect(st.exram.length).toBe(32768);
  expect(st.exram[0]).toBe(0xee);
});

test("decodeSaveState reuses decodeSniffer for the APU/PPU block", () => {
  const st = decodeSaveState(buildSaveState());
  expect(st.sniffer.magicOk).toBe(true);
  expect(st.sniffer.apu.enableReg).toBe(0x0f);
  expect(st.sniffer.ppu.ctrl).toBe(0x88);
  expect(st.sniffer.ppu.showSprites).toBe(true);
});

test("decodeSaveState rejects a missing magic and a short file", () => {
  const b = buildSaveState();
  b[0x18cf] = 0x00;
  expect(decodeSaveState(b).magicOk).toBe(false);
  expect(() => decodeSaveState(new Uint8Array(0x1000))).toThrow();
});
