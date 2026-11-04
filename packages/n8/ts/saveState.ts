// Decodes an Everdrive N8 save-state file (EDN8/gamedata/<rom>/NN.SAV, 48 KB) into a running game's FULL
// captured state - reaching what the live sniffer (sniffer.ts) CANNOT: WRAM, VRAM, the CPU registers
// (a/x/y/sp), CHR, and EXRAM. The N8's in-game menu writes these when the player triggers a save-state (a
// physical button combo); the host reads the file over USB (Edio.readFile) and decodes it here. Pure +
// host-agnostic like sniffer.ts (no C++ twin).
//
// The 48 KB SST layout (edn8-pro-pub everdrive.h; the 0x1800 region IS the live-sniffer layout, so we reuse
// decodeSniffer for it): WRAM +0x0000 (2K), VRAM +0x0800 (4K), then at +0x1800 the sniffer block (mapper regs,
// APU +0x1880, PPU palette +0x18A0, PPU regs +0x18C0, CPU a/x/y/sp +0x18C8, 'S' magic +0x18CF, OAM +0x1900),
// CHR +0x2000 (8K), EXRAM/FDS +0x4000 (32K). NOTE: the everdrive.h comment lists CPU regs at 0x19C8, but that
// offset is inside OAM and reads as sprite data; the real slot is 0x18C8 (verified on a real .SAV: sp=0xFA).

import { decodeSniffer, type SnifferSnapshot } from "./sniffer";

const OFF_WRAM = 0x0000;
const OFF_VRAM = 0x0800;
const OFF_SNIF = 0x1800; // the sniffer-layout block (mapper/APU/PPU/OAM/magic) starts here
const OFF_CPU = 0x18c8; // a, x, y, sp
const OFF_CHR = 0x2000;
const OFF_EXRAM = 0x4000;

const WRAM_SIZE = 0x800; // 2 KB
const VRAM_SIZE = 0x1000; // 4 KB
const CHR_SIZE = 0x2000; // 8 KB
const EXRAM_SIZE = 0x8000; // 32 KB
export const SAVESTATE_SIZE = 0xc000; // 48 KB

export interface N8CpuRegs {
  a: number;
  x: number;
  y: number;
  sp: number;
}

export interface N8SaveState {
  magicOk: boolean; // 'S' (0x53) at 0x18CF: a valid save-state
  cpu: N8CpuRegs; // the captured 6502 registers (NOT available from the live sniffer)
  sniffer: SnifferSnapshot; // APU / PPU regs / palette / OAM, decoded from the 0x1800 block (reused)
  wram: Uint8Array; // 2 KB console RAM ($0000-$07FF) - NOT in the live sniffer
  vram: Uint8Array; // 4 KB nametable RAM
  chr: Uint8Array; // 8 KB CHR
  exram: Uint8Array; // 32 KB EXRAM / FDS RAM
}

/** Decode a 48 KB N8 save-state (as read from an NN.SAV over USB). Throws if too short. */
export function decodeSaveState(bytes: Uint8Array): N8SaveState {
  if (bytes.length < SAVESTATE_SIZE)
    throw new Error(`save-state too short: ${bytes.length} < ${SAVESTATE_SIZE}`);
  // The 0x1800 block matches the live-sniffer region byte-for-byte (raw offset = file offset - 0x1800), so
  // decodeSniffer gives us mapper/APU/PPU/palette/OAM + the 'S' magic (at its +0xCF = file 0x18CF) for free.
  const sniffer = decodeSniffer(bytes.subarray(OFF_SNIF, OFF_SNIF + 0x200));
  return {
    magicOk: sniffer.magicOk,
    cpu: { a: bytes[OFF_CPU], x: bytes[OFF_CPU + 1], y: bytes[OFF_CPU + 2], sp: bytes[OFF_CPU + 3] },
    sniffer,
    wram: bytes.slice(OFF_WRAM, OFF_WRAM + WRAM_SIZE),
    vram: bytes.slice(OFF_VRAM, OFF_VRAM + VRAM_SIZE),
    chr: bytes.slice(OFF_CHR, OFF_CHR + CHR_SIZE),
    exram: bytes.slice(OFF_EXRAM, OFF_EXRAM + EXRAM_SIZE),
  };
}
