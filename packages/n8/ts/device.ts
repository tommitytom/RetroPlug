// The Everdrive N8 Pro as a session script opens it: the SDK's connectN8 (packages/retroplug/cli/sdk.ts) is
// openN8Device over the app's RPC call, so a consumer script uploads a file, boots a ROM or reads the live sniffer
// without spawning `retroplug-cli n8-load` and parsing what it printed. Deliberately a small surface over the session
// createN8Client opens (the facet's, n8/desktop/N8Rpc.hpp, where the one Edio runs): the SDK's declaration file is
// written by hand (packages/retroplug/cli/sdk-types.d.ts), and every type exposed here is one more to keep exact there.
// Every call is synchronous, as every facet call is, so none of it needs the event loop; a call that fails on the line
// leaves the next to resync the session in the host (N8Rpc::ready).

import { createN8Client, pickN8Port, ADDR_SSR, SNIFFER_REGION_SIZE, type N8Call, type N8DirEntry } from "./index";
import type { LoadOptions } from "./load";

export type { N8DirEntry } from "./index";

export interface N8ConnectOptions {
  /** Serial port to open. Omitted = the first port whose VID:PID is the N8 Pro's (38df:0017). */
  serial?: string;
  /** Handshake timeout, ms. */
  timeoutMs?: number;
}

/** What load() uploads and boots: ROM bytes to usb-games/<romName>, or a ROM already on the card, and a battery. */
export type N8LoadOptions = LoadOptions;

export interface N8Device {
  /** The serial port in use. */
  readonly port: string;
  /** Upload and boot a ROM by driving the N8's menu. Only works AT the menu: with a game running the menu is not there
   *  to answer, and the call times out (2 s). Power-cycle first (the cart cannot reset the console). NOT a way to ask
   *  "is a game running?": its first act is to send the menu's probe bytes ("*t") through the cart FIFO, and a running
   *  game reads those as its own input. `romName` must be a plain file name. */
  load(opts: N8LoadOptions): { bootPath: string; mapIndex: number };
  /** List a directory on the card ("/" or "" is the root). */
  listDir(path: string): N8DirEntry[];
  /** Read a whole file off the card. Slow: a USB round trip a 512-byte block. */
  readFile(path: string): Uint8Array;
  /** Write a whole file to the card, creating missing parent directories. No reboot needed. */
  writeFile(path: string, bytes: Uint8Array): void;
  makeDir(path: string): void;
  /** Delete a file or an empty directory. Permanent. */
  deleteFile(path: string): void;
  /** The live sniffer region (512 bytes: mapper registers, the $4000-$401F mirror at +$080, OAM...), the same bytes
   *  `n8-load --sniff-raw` writes. A system-region read, NOT a cart DMA, so it never disturbs a running game. */
  sniffRaw(): Uint8Array;
  /** Push raw bytes into the cart FIFO, where a running ROM reads them at $40F0. */
  writeFifo(bytes: Uint8Array): void;
  /** Release the port. Any later call on this device throws. */
  close(): void;
}

/** Open the N8 on `call` (the explicit port, else the first N8 the host lists) and run the connect handshake. Throws
 *  if there is no N8 or it does not answer, which is also what another process holding the (exclusive) port looks
 *  like; a session whose handshake failed is closed first. */
export function openN8Device(call: N8Call, opts: N8ConnectOptions = {}): N8Device {
  const client = createN8Client(call);
  const n8 = client.openN8(pickN8Port(client.listPorts(), opts.serial));
  try {
    n8.connect(opts.timeoutMs);
  } catch (e) {
    n8.close();
    throw e;
  }

  // Closed here, not only in the host: the host answers a closed handle with its own words ("no open N8 session"), and
  // a zero-byte write never reaches it at all, so without this a call after close() could "succeed" doing nothing.
  let closed = false;
  const guard = <T>(what: string, f: () => T): T => {
    if (closed) throw new Error(`N8Device.${what}: the device is closed`);
    return f();
  };

  return {
    port: n8.port,
    load: (o) =>
      guard("load", () => {
        const { bootPath, mapIndex } = n8.load(o);
        return { bootPath, mapIndex };
      }),
    listDir: (path) =>
      guard("listDir", () =>
        n8.listDir(path === "/" ? "" : path).map(({ name, size, isDir }) => ({ name, size, isDir })),
      ),
    readFile: (path) => guard("readFile", () => n8.readFile(path)),
    writeFile: (path, bytes) => guard("writeFile", () => n8.writeFile(path, bytes)),
    makeDir: (path) => guard("makeDir", () => n8.edio.dirMake(path)),
    deleteFile: (path) => guard("deleteFile", () => n8.edio.fileDelete(path)),
    sniffRaw: () => guard("sniffRaw", () => n8.edio.memRD(ADDR_SSR, SNIFFER_REGION_SIZE)),
    writeFifo: (bytes) => guard("writeFifo", () => n8.edio.fifoWR(bytes)),
    close: () => {
      if (closed) return;
      closed = true;
      n8.close();
    },
  };
}
