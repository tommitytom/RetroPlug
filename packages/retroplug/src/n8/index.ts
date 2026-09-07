// The TS Everdrive N8 Pro stack: the Edio protocol framing + on-device menu + ROM/save orchestration over an
// injected serial byte-transport. Everything here is pure TS + host-agnostic - the native side keeps only
// the serial transport (the `SerialRpcService` facet, reached from TS via createSerialClient() in
// ../realBackend.ts). This is also the scriptable surface: `createN8(transport)` gives a JS/TS caller the
// whole cart API (connect / menu / load / SRAM read+write / listDir) over any SerialTransport.

import { Edio, ADDR_CFG, SIZE_CFG } from "./edio";
import { decodeMapConfig, type N8MapConfig } from "./mapConfig";
import { N8Menu } from "./n8Menu";
import { loadRom, dumpSram, writeSramDirect, writeMemDirect, type LoadOptions, type LoadResult } from "./n8Load";
import type { SerialTransport } from "./transport";

export {
  Edio,
  N8TimeoutError,
  ADDR_SRM,
  ADDR_MENU_CHR,
  ADDR_PRG,
  ADDR_CHR,
  ADDR_SSR,
  ADDR_FIFO,
  ADDR_CFG,
  SIZE_CFG,
  CHR_RAM_OFFSET,
  SIZE_CHR_BANK,
  N8_OS_REGION,
  SIZE_SRM_GAME,
  FA_READ,
  FA_WRITE,
  FA_CREATE_ALWAYS,
  FS_MAKEPATH,
} from "./edio";
export type { N8DirEntry } from "./edio";
export { N8Menu } from "./n8Menu";
export { decodeMapConfig, resolveChrWindow, chrBankAddr, describeChrWindow } from "./mapConfig";
export type { N8MapConfig, N8Mirroring, ChrWindow, ChrBankSource } from "./mapConfig";
export { RisaSyncTranslator } from "./risaSyncTranslator";
export { loadRom, dumpSram, writeSramDirect, writeMemDirect, assertGameRegion, baseName } from "./n8Load";
export type { LoadOptions, LoadResult } from "./n8Load";
export type { SerialTransport, OpenSerialPort, SerialPortInfo, SerialClient } from "./transport";

/** A connected N8 session over a serial transport: the Edio + menu handles plus the bound orchestration.
 *  The scriptable entry point - a user script does `const n8 = createN8(port); n8.connect(); n8.load(...)`. */
export interface N8 {
  edio: Edio;
  menu: N8Menu;
  /** Run the Edio connect handshake (throws if the N8 doesn't answer). */
  connect(timeoutMs?: number): number;
  /** Upload/restore/install/boot a ROM by driving the menu. */
  load(opts: LoadOptions): LoadResult;
  /** Read the cart battery SRAM (64 KB game region). */
  dumpSram(): Uint8Array;
  /** Write a save straight to cart SRAM (running game only; corrupts the menu). Returns bytes written. */
  writeSramDirect(srm: Uint8Array): number;
  /** Write a block straight to device memory (ADDR_PRG/ADDR_CHR + offset) + verify. Live-patches a running
   *  game's code/graphics. Returns bytes written. */
  writeMemDirect(addr: number, data: Uint8Array): number;
  /** List an SD-card directory. */
  listDir(path: string): ReturnType<Edio["listDir"]>;
  /** Read a whole SD-card file by path (over USB). */
  readFile(path: string): Uint8Array;
  /** Write a whole file to an SD-card path (over USB), creating any missing parent directories. The
   *  inverse of readFile — how a data file gets onto the card without going through a save slot. */
  writeFile(path: string, bytes: Uint8Array): void;
  /** Read the FPGA mapper-config block: what the OS told the cart the RUNNING game is (mapper index,
   *  PRG/CHR/SRM sizes, mirroring, CHR-RAM-vs-ROM). */
  mapConfig(): N8MapConfig;
}

export function createN8(transport: SerialTransport): N8 {
  const edio = new Edio(transport);
  const menu = new N8Menu(edio);
  return {
    edio,
    menu,
    connect: (timeoutMs?: number) => edio.connect(timeoutMs),
    load: (opts: LoadOptions) => loadRom(edio, opts),
    dumpSram: () => dumpSram(edio),
    writeSramDirect: (srm: Uint8Array) => writeSramDirect(edio, srm),
    writeMemDirect: (addr: number, data: Uint8Array) => writeMemDirect(edio, addr, data),
    listDir: (path: string) => edio.listDir(path),
    readFile: (path: string) => edio.readFile(path),
    writeFile: (path: string, bytes: Uint8Array) => edio.writeFile(path, bytes),
    mapConfig: () => decodeMapConfig(edio.memRD(ADDR_CFG, SIZE_CFG)),
  };
}
