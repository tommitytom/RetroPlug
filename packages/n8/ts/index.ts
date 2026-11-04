// @retroplug/n8's TypeScript half (docs/modular-graph-phase-e2-design.md section 7): the client of the pack's RPC facet
// (packages/n8/native/src/n8/desktop/N8Rpc.hpp), the decoders of what the cart answers, and the load helpers. It takes
// the app's RPC call rather than importing the app's backend, so the pack depends on nothing of packages/retroplug.
//
// The facet has two halves. The streaming link is what the UI's Settings > N8 drives (ui/screens/menu/n8Devices.ts):
// the serial ports, the chosen one, the link's state and its lookahead, read and set on the host's N8 provider; the
// state lives in the native host (the serial link, n8.cfg), so every read is fresh. A session is what the CLI drives
// (cli/sessions/n8-*.ts): openN8(port) opens the port in the native host, and the N8 it answers runs the protocol there
// (the one Edio, n8/Edio.hpp), each call synchronous, as every facet's are. What a failed call throws is the protocol's
// own text (an Error, or an N8TimeoutError when the N8 did not answer), the words the CLI prints and BlipToaster and
// the hardware lab read.

import { ADDR_CFG, SIZE_CFG } from "./constants";
import { decodeMapConfig, type N8MapConfig } from "./mapConfig";
import { dumpSram, writeMemDirect, writeSramDirect, type LoadOptions, type LoadResult } from "./load";

export * from "./constants";
export { decodeMapConfig, resolveChrWindow, chrBankAddr, describeChrWindow } from "./mapConfig";
export type { N8MapConfig, N8Mirroring, ChrWindow, ChrBankSource } from "./mapConfig";
export { decodeSniffer, decodeSprites, SNIFFER_REGION_SIZE, CPU_HZ_NTSC, CPU_HZ_PAL } from "./sniffer";
export type { SnifferSnapshot } from "./sniffer";
export { decodeSysInfo, decodeVdc, fatDateStr, vdcStr, DEV_ID_N8_PRO } from "./sysInfo";
export { decodeSaveState, SAVESTATE_SIZE } from "./saveState";
export type { N8SaveState } from "./saveState";
export { menuScreenToRgba } from "./menuImage";
export { assertGameRegion, baseName, dumpSram, writeMemDirect, writeSramDirect } from "./load";
export type { LoadOptions, LoadResult, N8Memory } from "./load";

/** The app's synchronous RPC call (packages/retroplug/src/rpcClient.ts's Call): it answers the method's result, and
 *  throws when the method fails or the host has no such method. */
export type N8Call = (method: string, ...params: unknown[]) => unknown;

export interface N8Port {
  port: string; // OS serial port name (/dev/ttyACM0, COM3, ...)
  isN8: boolean; // detected Everdrive N8 Pro (USB VID:PID 38df:0017)
}

export interface N8LinkConfig {
  ports: N8Port[]; // available serial ports
  selectedPort: string; // "" = none chosen (connect auto-picks the attached N8)
  connected: boolean; // the serial link is open + handshaken
  enabled: boolean; // the user's "stream to the N8" toggle
  lookaheadMs: number; // timed-release latency the serial thread applies
  bytes: number; // bytes forwarded since connect (status)
  error: string; // last error, or "" (status)
}

/** The facet's link methods. Each applies at once in the host, which persists it to n8.cfg. */
export interface N8LinkClient {
  linkConfig(): N8LinkConfig;
  setPort(port: string): void;
  connect(enabled: boolean): void;
  setLookahead(ms: number): void;
}

/** Thrown when the N8 does not answer within the read timeout (no device / wrong port / wedged menu). */
export class N8TimeoutError extends Error {
  constructor(message = "Edio: serial read timeout (no N8 response)") {
    super(message);
    this.name = "N8TimeoutError";
  }
}

/** One SD-card directory entry (from listDir). */
export interface N8DirEntry {
  name: string;
  size: number;
  isDir: boolean;
}

/** The Edio protocol's calls on an open session, each one call of the facet's (N8Rpc.hpp), throwing on a failure. */
export interface N8Edio {
  /** The connect handshake: flushInput, then CMD_STATUS answered <status> A5 within `handshakeTimeoutMs` (default
   *  300). The status byte. */
  connect(handshakeTimeoutMs?: number): number;
  getStatus(): number;
  /** The raw 64-byte device-info block (decodeSysInfo) and the 8-byte board-voltage block (decodeVdc). */
  sysInfo(): Uint8Array;
  vdc(): Uint8Array;
  /** Free space on the SD card, in bytes. */
  freeSpace(): number;
  /** mkdir ("already exists" is done); delete a file or an empty directory. */
  dirMake(path: string): void;
  fileDelete(path: string): void;
  /** The reply wait (Timeouts::readMs), in ms (2000 until changed); the handshake, the file blocks and the install keep
   *  their own waits. */
  setReadTimeout(ms: number): void;
  flushInput(): void;
  /** Device memory: memWR is fire-and-forget; memRD blocks until `size` bytes arrive. */
  memWR(addr: number, data: Uint8Array): void;
  memRD(addr: number, size: number): Uint8Array;
  /** Bytes to the cart FIFO, and a length-prefixed string to it. */
  fifoWR(data: Uint8Array): void;
  fifoTxString(s: string): void;
  /** The SD-card directory and file API. */
  listDir(path: string): N8DirEntry[];
  fileOpen(path: string, mode: number): void;
  fileWrite(data: Uint8Array): void;
  fileClose(): void;
  fileRead(size: number): Uint8Array;
  /** A whole SD file by path, and a whole file to a path (its parent directories made). */
  readFile(path: string): Uint8Array;
  writeFile(path: string, bytes: Uint8Array): void;
  /** Raw reads with no command sent: a menu's reply. */
  rx8(): number;
  rx16(): number;
  rx32(): number;
  readData(size: number): Uint8Array;
}

/** The on-device menu's commands (n8/Menu.hpp), valid while the N8's file browser runs. */
export interface N8Menu {
  /** '*t', answered 'k': throws when the menu is not running. */
  test(): void;
  /** '*n' and the SD path: the ROM installed, its map index answered. */
  appInstall(devicePath: string): number;
  /** '*s': the installed ROM boots. */
  appStart(): void;
  /** '*v': the menu's 2048 VRAM bytes and 16 palette bytes (menuScreenToRgba). */
  vramDump(): { vram: Uint8Array; palette: Uint8Array };
  /** '*r': reboot to the menu (poll test() to know when it is back). */
  reset(): void;
}

/** An open session on one N8: the protocol's calls, the menu's, and the operations the CLI's sessions run. */
export interface N8 {
  /** The port the session is open on. */
  readonly port: string;
  edio: N8Edio;
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
  listDir(path: string): N8DirEntry[];
  /** Read a whole SD-card file by path (over USB). */
  readFile(path: string): Uint8Array;
  /** Write a whole file to an SD-card path (over USB), creating any missing parent directories. The
   *  inverse of readFile - how a data file gets onto the card without going through a save slot. */
  writeFile(path: string, bytes: Uint8Array): void;
  /** Read the FPGA mapper-config block: what the OS told the cart the RUNNING game is (mapper index,
   *  PRG/CHR/SRM sizes, mirroring, CHR-RAM-vs-ROM). */
  mapConfig(): N8MapConfig;
  /** Close the session and its port. */
  close(): void;
}

export interface N8Client {
  link: N8LinkClient;
  /** All serial ports, flagging Everdrive N8 units (USB VID:PID 38df:0017); [] from a host that mounted no N8 facet
   *  or answers nothing. */
  listPorts(): N8Port[];
  /** Open a session on `port`; throws (`cannot open serial port: <port>`) if it can't be opened. */
  openN8(port: string): N8;
}

// The reply read tolerantly, field by field, as the UI read the hook object it replaces: a host one version apart must
// not take the submenu down.
function readConfig(raw: unknown): N8LinkConfig {
  const c = (raw ?? {}) as Partial<Record<keyof N8LinkConfig, unknown>>;
  const ports = Array.isArray(c.ports)
    ? c.ports.map((p: { port?: unknown; isN8?: unknown } | null) => ({
        port: typeof p?.port === "string" ? p.port : "",
        isN8: !!p?.isN8,
      }))
    : [];
  return {
    ports,
    selectedPort: typeof c.selectedPort === "string" ? c.selectedPort : "",
    connected: !!c.connected,
    enabled: !!c.enabled,
    lookaheadMs: typeof c.lookaheadMs === "number" ? c.lookaheadMs : 0,
    bytes: typeof c.bytes === "number" ? c.bytes : typeof c.bytes === "bigint" ? Number(c.bytes) : 0,
    error: typeof c.error === "string" ? c.error : "",
  };
}

/** What every session call answers (N8Rpc.hpp's N8Reply). */
interface N8Reply {
  status: string;
  deviceStatus: number;
  message: string;
  bytes?: Uint8Array;
  value?: number | bigint;
  entries?: N8DirEntry[];
  step?: string;
}

// A reply's success, or the throw its failure is: an N8TimeoutError when the N8 did not answer, else an Error with the
// protocol's text.
function ok(reply: unknown): N8Reply {
  const r = reply as N8Reply | null | undefined;
  if (!r || typeof r.status !== "string") throw new Error("N8: the host answered no N8 reply");
  if (r.status === "ok") return r;
  if (r.status === "timeout") throw new N8TimeoutError(r.message || undefined);
  throw new Error(r.message || `N8: ${r.status}`);
}

const bytesOf = (r: N8Reply): Uint8Array => (r.bytes instanceof Uint8Array ? r.bytes : new Uint8Array(r.bytes ?? []));
const valueOf = (r: N8Reply): number => Number(r.value ?? 0);

function openSession(call: N8Call, port: string): N8 {
  const opened = call("n8Open", port) as N8Reply | undefined;
  if (!opened || opened.status !== "ok") throw new Error(opened?.message || `cannot open serial port: ${port}`);
  const h = valueOf(opened);
  const run = (method: string, ...params: unknown[]): N8Reply => ok(call(method, h, ...params));

  const edio: N8Edio = {
    connect: (handshakeTimeoutMs = 300) => valueOf(run("n8Handshake", handshakeTimeoutMs)),
    getStatus: () => valueOf(run("n8Status")),
    sysInfo: () => bytesOf(run("n8SysInfo")),
    vdc: () => bytesOf(run("n8Vdc")),
    freeSpace: () => valueOf(run("n8FreeSpace")),
    dirMake: (path) => void run("n8DirMake", path),
    fileDelete: (path) => void run("n8FileDelete", path),
    setReadTimeout: (ms) => void run("n8SetReadTimeout", Math.max(0, Math.trunc(ms))),
    flushInput: () => void run("n8FlushInput"),
    memWR: (addr, data) => {
      if (data.length) run("n8MemWR", addr, data);
    },
    memRD: (addr, size) => (size === 0 ? new Uint8Array(0) : bytesOf(run("n8MemRD", addr, size))),
    fifoWR: (data) => {
      if (data.length) run("n8FifoWR", data);
    },
    fifoTxString: (s) => void run("n8FifoTxString", s),
    listDir: (path) => run("n8ListDir", path).entries ?? [],
    fileOpen: (path, mode) => void run("n8FileOpen", path, mode),
    fileWrite: (data) => void run("n8FileWrite", data),
    fileClose: () => void run("n8FileClose"),
    fileRead: (size) => bytesOf(run("n8FileRead", size)),
    readFile: (path) => bytesOf(run("n8ReadFile", path)),
    writeFile: (path, bytes) => void run("n8WriteFile", path, bytes),
    rx8: () => bytesOf(run("n8ReadData", 1, 0))[0],
    rx16: () => {
      const b = bytesOf(run("n8ReadData", 2, 0));
      return b[0] | (b[1] << 8);
    },
    rx32: () => {
      const b = bytesOf(run("n8ReadData", 4, 0));
      return (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)) >>> 0;
    },
    readData: (size) => bytesOf(run("n8ReadData", size, 0)),
  };
  const menu: N8Menu = {
    test: () => void run("n8MenuTest"),
    appInstall: (devicePath) => valueOf(run("n8MenuInstall", devicePath)),
    appStart: () => void run("n8MenuStart"),
    vramDump: () => {
      const b = bytesOf(run("n8MenuVramDump"));
      return { vram: b.slice(0, 2048), palette: b.slice(2048, 2048 + 16) };
    },
    reset: () => void run("n8MenuReset"),
  };
  return {
    port,
    edio,
    menu,
    connect: (timeoutMs?: number) => edio.connect(timeoutMs),
    load: (opts: LoadOptions): LoadResult => {
      // Upload (optional) + restore-save (optional) + install + boot a ROM by driving the on-device menu, as one
      // operation in the host (Ops.hpp's bootRom): an SD path boots what the card holds, ROM bytes go to
      // usb-games/<romName> first. The session must already be connect()'d.
      let bootPath = opts.sdPath ?? "";
      if (!bootPath) {
        if (!opts.romBytes || !opts.romName)
          throw new Error("loadRom: pass romBytes + romName to upload, or sdPath to boot an existing SD ROM");
        bootPath = `usb-games/${opts.romName}`;
      }
      const upload = !opts.sdPath;
      const rom = upload ? opts.romBytes! : new Uint8Array(0);
      const srm = opts.srm ?? new Uint8Array(0);
      const mapIndex = valueOf(run("n8BootRom", bootPath, rom, srm, upload));
      return { bootPath, mapIndex };
    },
    dumpSram: () => dumpSram(edio),
    writeSramDirect: (srm: Uint8Array) => writeSramDirect(edio, srm),
    writeMemDirect: (addr: number, data: Uint8Array) => writeMemDirect(edio, addr, data),
    listDir: (path: string) => edio.listDir(path),
    readFile: (path: string) => edio.readFile(path),
    writeFile: (path: string, bytes: Uint8Array) => edio.writeFile(path, bytes),
    mapConfig: () => decodeMapConfig(edio.memRD(ADDR_CFG, SIZE_CFG)),
    close: () => void call("n8Close", h),
  };
}

/** A client of the N8 facet over `call`. Creating one calls nothing; a host that did not mount the facet makes each
 *  method throw (the call's own error). */
export function createN8Client(call: N8Call): N8Client {
  return {
    link: {
      linkConfig: () => readConfig(call("n8LinkConfig")),
      setPort: (port) => void call("n8SetPort", port),
      connect: (enabled) => void call("n8Connect", enabled),
      setLookahead: (ms) => void call("n8SetLookahead", Math.trunc(ms)),
    },
    listPorts: () => {
      let ports: N8Port[] | undefined;
      try {
        ports = call("n8ListPorts") as N8Port[] | undefined;
      } catch (e) {
        // A host that mounted no N8 facet answers method-not-found (JSON-RPC -32601): the CLI then says it found no
        // N8, as the serial client's [] did. Any other failure is the call's own, and reaches the caller.
        if (/-32601|Method not found/.test(e instanceof Error ? e.message : String(e))) return [];
        throw e;
      }
      return (ports ?? []).map((p) => ({ port: p.port, isN8: !!p.isN8 }));
    },
    openN8: (port) => openSession(call, port),
  };
}
