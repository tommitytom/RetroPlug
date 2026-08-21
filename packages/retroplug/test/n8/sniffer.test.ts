// The N8 live save-state sniffer decoder (src/n8/sniffer.ts): a 512-byte Edio.memRD(ADDR_SSR) region ->
// a running NES game's APU/PPU/OAM state. Pure, no hardware - hand-build a region with known register
// values (a pulse tuned to ~A4, a triangle, noise, PPU regs, a few on-screen sprites) and assert the decode.
import { test, expect } from "../../testing/harness";
import { decodeSniffer, decodeSprites, SNIFFER_REGION_SIZE, CPU_HZ_NTSC } from "../../src/n8/sniffer";

const APU = 0x080; // $4000-$401F write-mirror base
const PPU = 0x0c0; // ctrl, mask, scroll-x, scroll-y
const MAGIC = 0x0cf;
const OAM = 0x100;

// A fully-populated synthetic region: magic set, pulse1 tuned, triangle tuned, noise, PPU regs, 3 on-screen sprites.
function buildRegion(): Uint8Array {
  const r = new Uint8Array(SNIFFER_REGION_SIZE);
  r[MAGIC] = 0x53; // 'S'

  // Pulse1: $4000 = duty 2 (50%), constant volume, volume 7 -> 0x80|0x10|0x07 = 0x97.
  r[APU + 0x00] = 0x97;
  // timer 253 -> freq = 1789773 / (16*254) = 440.4 -> 440 Hz (~A4). $4002 low, $4003 high(bits0-2).
  r[APU + 0x02] = 253;
  r[APU + 0x03] = 0x00;
  // Triangle: timer 126 -> 1789773 / (32*127) = 440.4 -> 440 Hz. $400A low, $400B high.
  r[APU + 0x0a] = 126;
  r[APU + 0x0b] = 0x00;
  r[APU + 0x08] = 0x80; // $4008 control (length-halt) bit
  // Noise: $400C = 0x1A (const vol, volume 0x0A); $400E = 0x83 (short mode, period index 3).
  r[APU + 0x0c] = 0x1a;
  r[APU + 0x0e] = 0x83;
  // Enables ($4015) = pulse1|pulse2|tri|noise; DMC off. Frame ($4017) = 4-step.
  r[APU + 0x15] = 0x0f;
  r[APU + 0x17] = 0x00;

  // PPU: ctrl 0x90, mask 0x1E (show bg + show sprites), scroll (0x12, 0x34).
  r[PPU + 0x00] = 0x90;
  r[PPU + 0x01] = 0x1e;
  r[PPU + 0x02] = 0x12;
  r[PPU + 0x03] = 0x34;

  // Palette: first entry a known colour.
  r[0x0a0] = 0x0f;
  r[0x0a1] = 0x21;

  // OAM: all sprites off-screen (y=0xF0) except 3.
  for (let i = 0; i < 64; i++) r[OAM + i * 4] = 0xf0;
  r[OAM + 0 * 4] = 0x40;
  r[OAM + 1 * 4] = 0x50;
  r[OAM + 2 * 4] = 0x60;
  r[OAM + 2 * 4 + 1] = 0x2a; // sprite 2 tile
  return r;
}

test("decodeSniffer decodes the APU channels (pulse tuned to ~A4, triangle, noise)", () => {
  const s = decodeSniffer(buildRegion());
  expect(s.magicOk).toBe(true);

  expect(s.apu.pulse1.enabled).toBe(true);
  expect(s.apu.pulse1.duty).toBe(2);
  expect(s.apu.pulse1.volume).toBe(7);
  expect(s.apu.pulse1.constVol).toBe(true);
  expect(s.apu.pulse1.timer).toBe(253);
  expect(s.apu.pulse1.frequency).toBe(440);

  // pulse2 is enabled ($4015 bit1) but its registers are all zero -> timer 0 -> muted (freq 0).
  expect(s.apu.pulse2.enabled).toBe(true);
  expect(s.apu.pulse2.frequency).toBe(0);

  expect(s.apu.triangle.enabled).toBe(true);
  expect(s.apu.triangle.timer).toBe(126);
  expect(s.apu.triangle.frequency).toBe(440);

  expect(s.apu.noise.enabled).toBe(true);
  expect(s.apu.noise.volume).toBe(0x0a);
  expect(s.apu.noise.mode).toBe(true);
  expect(s.apu.noise.periodIndex).toBe(3);

  expect(s.apu.dmc.enabled).toBe(false);
  expect(s.apu.enableReg).toBe(0x0f);
  expect(s.apu.frameMode5Step).toBe(false);
});

test("decodeSniffer decodes PPU regs, palette, and on-screen sprite count", () => {
  const s = decodeSniffer(buildRegion());
  expect(s.ppu.ctrl).toBe(0x90);
  expect(s.ppu.mask).toBe(0x1e);
  expect(s.ppu.showBackground).toBe(true);
  expect(s.ppu.showSprites).toBe(true);
  expect(s.ppu.scrollX).toBe(0x12);
  expect(s.ppu.scrollY).toBe(0x34);

  expect(s.palette.length).toBe(32);
  expect(s.palette[0]).toBe(0x0f);
  expect(s.palette[1]).toBe(0x21);

  expect(s.oam.length).toBe(256);
  expect(s.activeSprites).toBe(3); // y < 0xEF

  const sprites = decodeSprites(s.oam);
  expect(sprites.length).toBe(64);
  expect(sprites[2]).toEqual({ index: 2, y: 0x60, tile: 0x2a, attr: 0, x: 0 });
});

test("decodeSniffer flags a missing magic (no running game / read at the menu)", () => {
  const r = buildRegion();
  r[MAGIC] = 0x00; // sniffer off (menu) -> no 'S'
  expect(decodeSniffer(r).magicOk).toBe(false);
});

test("decodeSniffer honours a PAL clock and rejects a short region", () => {
  const s = decodeSniffer(buildRegion(), 1662607); // PAL 2A07
  expect(s.apu.pulse1.frequency).toBe(Math.round(1662607 / (16 * 254)));
  expect(() => decodeSniffer(new Uint8Array(0x100))).toThrow();
  expect(CPU_HZ_NTSC).toBe(1789773);
});
