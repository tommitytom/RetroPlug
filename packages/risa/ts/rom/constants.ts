// risa's own ROM layout: how its banks are laid out and where its resident kit-metadata mirror sits. The shared
// FORMAT numbers (the iNES header and PRG sizes, the kit bank, the CHR bank, the theme record) are @retroplug/tracker's
// - BlipToaster's ROM carries the same formats - and are imported from there; what is here is risa's, because a bank
// count and a mirror are one cartridge's design, not a format. Offsets are still DERIVED from the iNES header at
// runtime (see rom.ts computeLayout); nothing here hard-codes a per-ROM address.

import { HEADER_SIZE, PRG_16K_SIZE } from "@retroplug/tracker/nes/constants";

export const PRG_FIXED_SIZE = 0x2000; // the fixed bank (theme table lives here)
export const KIT_BANK_COUNT = 32; // the 32 DPCM kit banks at the top of PRG
export const FONT_BANK_COUNT = 4;

// The resident kit-metadata MIRROR (a flat copy so the UI needn't page banks): a 6-byte magic then
// kit_names[32][16] + kit_sample_names[32][48] + kit_slot_present[32][16]. Located by scan (the hints miss
// in 2.2.1 - the mirror sits at 0x215EA); the slot-present sub-table must be all 0/1 to accept a candidate.
export const KIT_META_MAGIC = [0xa5, 0x5a, 0x4b, 0x54, 0x4d, 0x45]; // "·ZKTME"
export const KIT_META_NAMES_STRIDE = 16;
export const KIT_META_SAMPLE_NAMES_STRIDE = 48; // 16 slots x 3
export const KIT_META_SLOT_PRESENT_STRIDE = 16;
export const KIT_META_NAMES_SIZE = 32 * 16;
export const KIT_META_SAMPLE_NAMES_SIZE = 32 * 48;
export const KIT_META_SLOT_PRESENT_SIZE = 32 * 16;
export const KIT_META_TOTAL_SIZE = 6 + KIT_META_NAMES_SIZE + KIT_META_SAMPLE_NAMES_SIZE + KIT_META_SLOT_PRESENT_SIZE; // 2566
export const KIT_META_HINT_OFFSETS = [HEADER_SIZE + 8 * PRG_16K_SIZE, HEADER_SIZE + 4 * PRG_16K_SIZE]; // 0x20010, 0x10010
