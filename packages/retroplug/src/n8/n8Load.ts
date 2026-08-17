// The N8 ROM/save orchestration, in TS - a faithful port of the cli/N8Load.cpp menu path over the TS Edio +
// N8Menu. Pure over an already-connect()'d Edio (file bytes are read/written by the caller), so it is unit-
// testable against a fake transport. The CLI `n8` command group (cli/sessions/n8.ts) wires local file I/O +
// the serial port around these.

import { Edio, ADDR_SRM, SIZE_SRM_GAME, N8_OS_REGION, FA_WRITE, FA_CREATE_ALWAYS, FS_MAKEPATH } from "./edio";
import { N8Menu } from "./n8Menu";

/** The final path component (basename) of an SD or local path. */
export const baseName = (path: string): string => {
  const i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return i < 0 ? path : path.slice(i + 1);
};

export interface LoadOptions {
  /** Local ROM bytes to upload to usb-games/<romName>. Omit to boot an existing SD path (sdPath). */
  romBytes?: Uint8Array;
  /** Upload target basename under usb-games/ (required with romBytes). */
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

// Upload (optional) + restore-save (optional) + install + boot a ROM by driving the on-device menu. The
// Edio must already be connect()'d. Mirrors runN8Load's menu path in cli/N8Load.cpp.
export function loadRom(edio: Edio, opts: LoadOptions): LoadResult {
  const menu = new N8Menu(edio);
  menu.test(); // confirm the menu is running (a running game won't answer '*t')

  let bootPath = opts.sdPath ?? "";
  if (!bootPath) {
    if (!opts.romBytes || !opts.romName)
      throw new Error("loadRom: pass romBytes + romName to upload, or sdPath to boot an existing SD ROM");
    bootPath = `usb-games/${opts.romName}`;
    edio.fileOpen(bootPath, FA_WRITE | FA_CREATE_ALWAYS | FS_MAKEPATH);
    edio.fileWrite(opts.romBytes);
    edio.fileClose();
  }

  // Restore the battery save the NATIVE way: write it into the menu's per-game save slot
  // (EDN8/gamedata/<rom>/bram.srm) BEFORE the menu loads the game, so the MENU itself copies it into cart
  // SRAM at hand-off. Writing cart SRAM directly over USB corrupts the running menu (which uses that region).
  if (opts.srm && opts.srm.length) {
    const gd = `EDN8/gamedata/${baseName(bootPath)}/bram.srm`;
    edio.fileOpen(gd, FA_WRITE | FA_CREATE_ALWAYS | FS_MAKEPATH);
    edio.fileWrite(opts.srm);
    edio.fileClose();
  }

  const mapIndex = menu.appInstall(bootPath);
  menu.appStart();
  return { bootPath, mapIndex };
}

// Read the cart SRAM (game battery region) out. Works on a running game; captures a game's native on-cart
// save for comparison / conversion.
export function dumpSram(edio: Edio): Uint8Array {
  return edio.memRD(ADDR_SRM, SIZE_SRM_GAME);
}

// Write a save STRAIGHT to cart SRAM over USB (no ROM/menu/reboot), for a game already running, and verify
// the readback. WARNING: corrupts the menu if run while the MENU (not a game) is active. Returns bytes written.
export function writeSramDirect(edio: Edio, srm: Uint8Array): number {
  const n = Math.min(srm.length, SIZE_SRM_GAME);
  const data = srm.subarray(0, n);
  edio.memWR(ADDR_SRM, data);
  const check = edio.memRD(ADDR_SRM, n);
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
export function writeMemDirect(edio: Edio, addr: number, data: Uint8Array): number {
  if (data.length === 0) return 0;
  edio.memWR(addr, data);
  const check = edio.memRD(addr, data.length);
  for (let i = 0; i < data.length; i++)
    if (check[i] !== data[i]) throw new Error(`device write verify failed at 0x${(addr + i).toString(16)}`);
  return data.length;
}
