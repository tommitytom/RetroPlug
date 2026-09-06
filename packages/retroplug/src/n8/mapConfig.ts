// Decodes the Everdrive N8 Pro's live FPGA mapper-config block (Edio ADDR_CFG, 16 bytes) and resolves which
// slice of the CHR chip the running game's PPU is actually looking at. Pure + host-agnostic like sniffer.ts
// (decoding, not protocol - no C++ twin).
//
// The N8 OS writes this block when it hands a game over, so it describes the RUNNING cart rather than any
// file on disk: mapper index, the PRG/CHR/SRM size masks, mirroring, and - the reason this module exists -
// whether CHR is ROM or RAM. Byte layout from krikzz edn8-pro-pub fpga/base_sv/sys_cfg.sv:
//
//   [0] map_idx low 8      [1] prg_msk: lo nibble PRG (8 KB units), hi nibble SRM (128 B units)
//   [2] lo nibble CHR mask (8 KB units), hi nibble map_idx[11:8]      [3] master_vol
//   [4] map_cfg: bits 1:0 mirroring, bit 2 CHR-RAM, bit 3 PRG-RAM off, bits 7:4 mapper sub-config
//   [5] ss_key_save  [6] ss_key_load  [7] ctrl  [8] ss_key_menu  [9] jumper
//
// Each mask nibble n means "1 << n units", i.e. the cart is masked to that many banks.

import { ADDR_CHR, CHR_RAM_OFFSET, SIZE_CFG, SIZE_CHR_BANK } from "./edio";

export type N8Mirroring = "horizontal" | "vertical" | "four-screen" | "single";

const MIRRORING: N8Mirroring[] = ["horizontal", "vertical", "four-screen", "single"];

export interface N8MapConfig {
  mapIdx: number; // 12-bit N8 mapper index (usually the iNES mapper number)
  prgSizeBytes: number; // PRG window the cart is masked to
  chrBanks: number; // number of 8 KB CHR banks
  chrSizeBytes: number; // chrBanks * 8 KB
  chrRam: boolean; // map_cfg bit 2: CHR is cart RAM (upper 4 MB of the chip), not ROM
  srmSizeBytes: number; // battery RAM the cart is masked to
  prgRamOff: boolean; // map_cfg bit 3
  mirroring: N8Mirroring;
  mapSub: number; // map_cfg bits 7:4 - per-mapper sub-configuration
  masterVol: number; // expansion-audio volume (128 = unity)
  unlocked: boolean; // ctrl bit 7: a game core is configured (the OS clears it to force map 255)
}

/** Decode the 16-byte block read from Edio ADDR_CFG. Throws if it is short. */
export function decodeMapConfig(bytes: Uint8Array): N8MapConfig {
  if (bytes.length < SIZE_CFG) throw new Error(`N8 config block too short: ${bytes.length} < ${SIZE_CFG}`);
  const mapCfg = bytes[4];
  const chrBanks = 1 << (bytes[2] & 0x0f);
  return {
    mapIdx: ((bytes[2] >> 4) << 8) | bytes[0],
    prgSizeBytes: (1 << (bytes[1] & 0x0f)) * 0x2000,
    chrBanks,
    chrSizeBytes: chrBanks * SIZE_CHR_BANK,
    chrRam: (mapCfg & 0x04) !== 0,
    srmSizeBytes: (1 << (bytes[1] >> 4)) * 0x80,
    prgRamOff: (mapCfg & 0x08) !== 0,
    mirroring: MIRRORING[mapCfg & 0x03],
    mapSub: mapCfg >> 4,
    masterVol: bytes[3],
    unlocked: (bytes[7] & 0x80) !== 0,
  };
}

// Which 8 KB CHR bank the PPU currently sees, read out of the live mapper-register mirror (sniffer +0x000,
// SnifferSnapshot.mapperRegs). This is unavoidably per-mapper: the FPGA core decides what it publishes there,
// and plenty of mappers (MMC3 and friends) don't have a single 8 KB CHR bank at all. So this table stays an
// allowlist of layouts verified on real hardware - an unknown mapper reports "don't know" and the caller asks
// for --chr-bank rather than guessing a bank and returning confident nonsense.
const CHR_BANK_DECODERS: Record<number, (regs: Uint8Array) => number> = {
  // UNROM 512: one latch written to $8000-$FFFF, CHR-RAM bank in bits 5-6 (PRG bank in 0-4, mirroring in 7).
  30: (regs) => (regs[0] >> 5) & 0x03,
};

/** Where the visible 8 KB CHR bank was sourced from. */
export type ChrBankSource =
  | "single" // the cart has exactly one bank, so there is nothing to choose
  | "mapper" // decoded from the live mapper registers
  | "explicit"; // the caller passed --chr-bank

export interface ChrWindow {
  addr: number; // absolute device address of the visible 8 KB bank (pass to memRD/memWR)
  bank: number;
  bankCount: number;
  ram: boolean; // CHR-RAM (upper 4 MB) rather than CHR-ROM
  source: ChrBankSource;
}

/** Device address of CHR bank `bank` on the running cart: CHR-ROM lives at the bottom of the CHR chip,
 *  CHR-RAM at CHR_RAM_OFFSET (see edio.ts). */
export const chrBankAddr = (cfg: N8MapConfig, bank: number): number =>
  ADDR_CHR + (cfg.chrRam ? CHR_RAM_OFFSET : 0) + bank * SIZE_CHR_BANK;

/** Resolve the 8 KB CHR window the running game's PPU is fetching from, so a dump or a live patch lands on
 *  the pixels actually on screen. `mapperRegs` is SnifferSnapshot.mapperRegs; `explicitBank` is --chr-bank.
 *  Throws when the bank is genuinely unknown - never falls back to bank 0, which is what made a CHR-RAM dump
 *  silently return the N8 OS font. */
export function resolveChrWindow(cfg: N8MapConfig, mapperRegs: Uint8Array, explicitBank?: number): ChrWindow {
  const bankCount = cfg.chrBanks;
  const window = (bank: number, source: ChrBankSource): ChrWindow => ({
    addr: chrBankAddr(cfg, bank),
    bank,
    bankCount,
    ram: cfg.chrRam,
    source,
  });

  if (explicitBank !== undefined) {
    if (!Number.isInteger(explicitBank) || explicitBank < 0 || explicitBank >= bankCount)
      throw new Error(`--chr-bank ${explicitBank} is out of range: this cart has ${bankCount} CHR bank(s) (0-${bankCount - 1})`);
    return window(explicitBank, "explicit");
  }
  if (bankCount === 1) return window(0, "single");

  const decoded = CHR_BANK_DECODERS[cfg.mapIdx]?.(mapperRegs);
  if (decoded === undefined)
    throw new Error(
      `cannot tell which of this cart's ${bankCount} CHR banks is on screen: mapper ${cfg.mapIdx}'s CHR ` +
        `banking is not decoded here. Pass --chr-bank <0-${bankCount - 1}> to pick one`,
    );
  return window(decoded % bankCount, "mapper");
}

/** One-line summary of a resolved window, for the CLI to print instead of a hardcoded "bank 0". */
export function describeChrWindow(w: ChrWindow): string {
  const where = w.bankCount > 1 ? `bank ${w.bank}/${w.bankCount}` : "the only bank";
  const how = w.source === "mapper" ? " (live mapper regs)" : w.source === "explicit" ? " (--chr-bank)" : "";
  return `CHR-${w.ram ? "RAM" : "ROM"} ${where}${how} @ 0x${w.addr.toString(16)}`;
}
