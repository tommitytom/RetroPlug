// The N8 FPGA mapper-config decoder + CHR window resolver (src/n8/mapConfig.ts). Pure, no hardware.
//
// The CHR_RAM fixture is the literal 16 bytes read off the real N8 over USB while the nesvj ROM (iNES mapper
// 30, 64 KB PRG, 32 KB CHR-RAM, vertical mirroring) was running, so the decode is pinned to a device that
// really produced it. It is also the regression fixture for the bug this module exists to close: --dump-chr
// read CHR bank 0 at ADDR_CHR for every cart, which on a CHR-RAM cart is not the game's pixels at all but
// leftover N8 OS file-browser font - 8192 plausible bytes, no error.
import { test, expect } from "../../testing/harness";
import { decodeMapConfig, resolveChrWindow, chrBankAddr, describeChrWindow } from "../../src/n8/mapConfig";
import { ADDR_CHR, CHR_RAM_OFFSET } from "../../src/n8/edio";

// Real device bytes: map_idx 30, PRG 1<<3 banks, SRM 1<<6 units, CHR 1<<2 banks, vol 0x80, map_cfg 0x05
// (vertical + CHR-RAM), ctrl 0x8E (unlocked).
const CHR_RAM_CFG = new Uint8Array([0x1e, 0x63, 0x02, 0x80, 0x05, 0x00, 0x00, 0x8e, 0x14, 0, 0, 0, 0, 0, 0, 0]);
// A plain NROM-shaped cart: mapper 0, one 8 KB CHR-ROM bank, horizontal mirroring.
const CHR_ROM_CFG = new Uint8Array([0x00, 0x11, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0, 0, 0, 0, 0, 0, 0, 0]);

// Live mapper-register mirror as read from the running nesvj: the UNROM 512 latch, CHR bank in bits 5-6.
const regs = (latch: number): Uint8Array => {
  const r = new Uint8Array(16);
  r[0] = latch;
  return r;
};

test("decodes a CHR-RAM cart's config (real mapper-30 device bytes)", () => {
  const cfg = decodeMapConfig(CHR_RAM_CFG);
  expect(cfg.mapIdx).toBe(30);
  expect(cfg.chrRam).toBe(true);
  expect(cfg.chrBanks).toBe(4);
  expect(cfg.chrSizeBytes).toBe(32 * 1024);
  expect(cfg.prgSizeBytes).toBe(64 * 1024);
  expect(cfg.srmSizeBytes).toBe(8 * 1024);
  expect(cfg.mirroring).toBe("vertical");
  expect(cfg.masterVol).toBe(128);
  expect(cfg.unlocked).toBe(true);
});

test("decodes a CHR-ROM cart's config", () => {
  const cfg = decodeMapConfig(CHR_ROM_CFG);
  expect(cfg.mapIdx).toBe(0);
  expect(cfg.chrRam).toBe(false);
  expect(cfg.chrBanks).toBe(1);
  expect(cfg.mirroring).toBe("horizontal");
});

test("map_idx is 12-bit: the high nibble comes from the CHR-mask byte", () => {
  const b = new Uint8Array(CHR_ROM_CFG);
  b[0] = 0x05;
  b[2] = 0x20; // high nibble 2 -> 0x205 = 517
  expect(decodeMapConfig(b).mapIdx).toBe(517);
});

test("a short config block throws rather than decoding garbage", () => {
  expect(() => decodeMapConfig(new Uint8Array(8))).toThrow();
});

test("CHR-RAM banks are addressed in the upper 4 MB of the CHR chip, not at ADDR_CHR", () => {
  const cfg = decodeMapConfig(CHR_RAM_CFG);
  // The whole bug in one assertion: bank 0 of a CHR-RAM cart is NOT ADDR_CHR.
  expect(chrBankAddr(cfg, 0)).toBe(ADDR_CHR + CHR_RAM_OFFSET);
  expect(chrBankAddr(cfg, 0) === ADDR_CHR).toBe(false);
  expect(chrBankAddr(cfg, 3)).toBe(ADDR_CHR + CHR_RAM_OFFSET + 3 * 0x2000);
  expect(chrBankAddr(decodeMapConfig(CHR_ROM_CFG), 0)).toBe(ADDR_CHR);
});

test("the visible bank comes from the live mapper registers on a mapper-30 cart", () => {
  const cfg = decodeMapConfig(CHR_RAM_CFG);
  // 0x60 is the latch value read off the running console: bits 5-6 = 3 -> CHR bank 3.
  const w = resolveChrWindow(cfg, regs(0x60));
  expect(w.bank).toBe(3);
  expect(w.bankCount).toBe(4);
  expect(w.ram).toBe(true);
  expect(w.source).toBe("mapper");
  expect(w.addr).toBe(ADDR_CHR + CHR_RAM_OFFSET + 3 * 0x2000);
  // The PRG bank in bits 0-4 must not leak into the CHR bank.
  expect(resolveChrWindow(cfg, regs(0x1f)).bank).toBe(0);
  expect(resolveChrWindow(cfg, regs(0x2a)).bank).toBe(1);
});

test("a single-bank cart needs no mapper knowledge", () => {
  const w = resolveChrWindow(decodeMapConfig(CHR_ROM_CFG), regs(0xff));
  expect(w.bank).toBe(0);
  expect(w.source).toBe("single");
  expect(w.addr).toBe(ADDR_CHR);
});

test("an undecoded multi-bank mapper refuses to guess, and names the escape hatch", () => {
  const b = new Uint8Array(CHR_RAM_CFG);
  b[0] = 4; // MMC3: 4 CHR banks, but no single visible 8 KB bank to report
  const cfg = decodeMapConfig(b);
  expect(() => resolveChrWindow(cfg, regs(0))).toThrow(/--chr-bank <0-3>/);
});

test("--chr-bank overrides the mapper decode and is range-checked", () => {
  const cfg = decodeMapConfig(CHR_RAM_CFG);
  const w = resolveChrWindow(cfg, regs(0x60), 1);
  expect(w.bank).toBe(1);
  expect(w.source).toBe("explicit");
  expect(w.addr).toBe(ADDR_CHR + CHR_RAM_OFFSET + 0x2000);
  expect(() => resolveChrWindow(cfg, regs(0x60), 4)).toThrow(/out of range/);
  expect(() => resolveChrWindow(cfg, regs(0x60), -1)).toThrow(/out of range/);
});

test("the window description says RAM-vs-ROM and the real bank, never a hardcoded 'bank 0'", () => {
  const cfg = decodeMapConfig(CHR_RAM_CFG);
  expect(describeChrWindow(resolveChrWindow(cfg, regs(0x60)))).toBe("CHR-RAM bank 3/4 (live mapper regs) @ 0xc06000");
  expect(describeChrWindow(resolveChrWindow(decodeMapConfig(CHR_ROM_CFG), regs(0)))).toBe(
    "CHR-ROM the only bank @ 0x800000",
  );
});
