// The Everdrive N8 Pro's device addresses, sizes and SD file-open flags, as the TS half of the pack names them: the
// twins of the constants on the native Edio (packages/n8/native/src/n8/Edio.hpp), which is where the protocol itself
// lives. A session's calls (createN8Client's openN8) take and answer these.

export const ADDR_PRG = 0x000000; // PRG-ROM PSRAM (8 MB) - live code, the same chip the CPU fetches
export const ADDR_CHR = 0x800000; // CHR-ROM PSRAM (8 MB) - live graphics, the same chip the PPU fetches
export const ADDR_SRM = 0x1000000; // cart battery RAM (a game's .srm)
export const ADDR_MENU_CHR = 0xfe0000; // menu CHR (ADDR_CHR 0x800000 + 0x7E0000); screenshot
export const N8_OS_REGION = 0x7e0000; // top of PRG/CHR (0x7E0000..0x800000) is the N8 OS/menu - never patch into it
export const ADDR_SSR = 0x1802000; // save-state sniffer: a running game's live APU/PPU/OAM write-mirror
export const ADDR_FIFO = 0x1810000; // cart FIFO (NES side reads $40F0/$40F1)
// The 16-byte FPGA mapper-config block (krikzz edn8-pro-pub fpga/base_sv/sys_cfg.sv `scfg`, decoded by
// mapConfig.ts; = everdrive.h's MapConfig struct minus its leading 32-byte cheat block, which is why the
// NES-CPU-side offsets in that header are 32 lower than the USB ones). The OS writes it when it hands a game
// over, so it describes the RUNNING cart: mapper index, PRG/CHR/SRM sizes, mirroring, and CHR-RAM-vs-ROM.
// Readable AND writable over USB (`pi_di` muxes `ce_cfg` back to the host in everdrive.sv) - HW-verified.
export const ADDR_CFG = 0x1800020;
export const SIZE_CFG = 16;
// Expansion-audio master volume = MapConfig.master_vol (scfg[3]). 0 = mute, 128 = unity, 255 = 2x. Live-only:
// it applies to the RUNNING cart and a reboot/map-load reloads the OS's stored value.
export const ADDR_EXP_VOL = ADDR_CFG + 3;
// A CHR-RAM cart's RAM is NOT at ADDR_CHR: everdrive.sv forces bit 22 of the CHR address for a chr_ram game
// ("save state engine expects chr ram to be mapped at upper 4M"), so the PPU sees the UPPER 4 MB of the CHR
// chip. That bit is applied only to the console's own fetches (`!dma.req_chr`) - the USB/DMA path passes the
// host address through verbatim, so a host read has to add this offset itself.
export const CHR_RAM_OFFSET = 0x400000;
export const SIZE_CHR_BANK = 0x2000; // 8 KB - one CHR bank / the whole PPU pattern-table window
export const SIZE_SRM_GAME = 0x10000; // 64 KB - max battery RAM a game uses

// File-open mode flags (FatFs).
export const FA_READ = 0x01;
export const FA_WRITE = 0x02;
export const FA_CREATE_ALWAYS = 0x08;
export const FS_MAKEPATH = 0x80; // create parent dirs if missing
