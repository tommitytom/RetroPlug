// The N8 ROM/save helpers the CLI's sessions run beside a session's protocol calls: a ROM booted through the menu is
// the session's load() (the host's bootRom, n8/Ops.hpp), and what is here is the rest, pure over a session's memory
// calls, so a fake session tests it: the battery read and the direct writes with their read-back, and the path and
// region guards.

import { ADDR_SRM, N8_OS_REGION, SIZE_SRM_GAME } from "./constants";

/** The memory calls of a session (N8Edio's) the helpers run on. */
export interface N8Memory {
  memWR(addr: number, data: Uint8Array): void;
  memRD(addr: number, size: number): Uint8Array;
}

/** The final path component (basename) of an SD or local path. */
export const baseName = (path: string): string => {
  const i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return i < 0 ? path : path.slice(i + 1);
};

export interface LoadOptions {
  /** Local ROM bytes to upload to usb-games/<romName>. Omit to boot an existing SD path (sdPath). */
  romBytes?: Uint8Array;
  /** Upload target basename under usb-games/ (required with romBytes). A plain file name: one holding a separator, a
   *  control byte or a dot directory is refused. */
  romName?: string;
  /** Boot a ROM already on the SD card by its SD path (instead of uploading). */
  sdPath?: string;
  /** Battery save (.srm) to restore via the menu's per-game slot before the game boots. */
  srm?: Uint8Array;
}

export interface LoadResult {
  bootPath: string;
  mapIndex: number;
}

// Read the cart SRAM (game battery region) out. Works on a running game; captures a game's native on-cart
// save for comparison / conversion.
export function dumpSram(n8: N8Memory): Uint8Array {
  return n8.memRD(ADDR_SRM, SIZE_SRM_GAME);
}

// Write a save STRAIGHT to cart SRAM over USB (no ROM/menu/reboot), for a game already running, and verify
// the readback. WARNING: corrupts the menu if run while the MENU (not a game) is active. Returns bytes written.
export function writeSramDirect(n8: N8Memory, srm: Uint8Array): number {
  const n = Math.min(srm.length, SIZE_SRM_GAME);
  const data = srm.subarray(0, n);
  n8.memWR(ADDR_SRM, data);
  const check = n8.memRD(ADDR_SRM, n);
  for (let i = 0; i < n; i++)
    if (check[i] !== data[i]) throw new Error("cart SRAM verify failed (readback != save)");
  return n;
}

// Guard a PRG/CHR patch offset+length against the N8 OS/menu region (top 0x7E0000..0x800000 of each chip):
// clobbering it would break the file browser until a power-cycle. Pure - throws on an out-of-range patch.
export function assertGameRegion(offset: number, len: number): void {
  if (!Number.isInteger(offset) || offset < 0)
    throw new Error(`patch offset must be a non-negative integer (got ${offset})`);
  if (offset + len > N8_OS_REGION)
    throw new Error(
      `patch [0x${offset.toString(16)}..0x${(offset + len).toString(16)}] runs into the N8 OS region ` +
        `(>= 0x${N8_OS_REGION.toString(16)}); keep game patches below it`,
    );
}

// Write a block STRAIGHT to device memory over USB (no menu/reboot) and verify the readback. The write-twin of
// dumpSram/memRD: patch a RUNNING game's PRG/CHR (ADDR_PRG/ADDR_CHR + offset) - the console fetches the same
// PSRAM, so the change is live on the next CPU/PPU fetch. Returns bytes written; throws if the readback differs.
export function writeMemDirect(n8: N8Memory, addr: number, data: Uint8Array): number {
  if (data.length === 0) return 0;
  n8.memWR(addr, data);
  const check = n8.memRD(addr, data.length);
  for (let i = 0; i < data.length; i++)
    if (check[i] !== data[i]) throw new Error(`device write verify failed at 0x${(addr + i).toString(16)}`);
  return data.length;
}
