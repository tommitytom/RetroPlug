// krikzz Everdrive N8 Pro USB client - a faithful TS port of packages/native/src/host/n8/Edio.{hpp,cpp}.
// The framing is asserted byte-for-byte against the native FakeSerialPort test (test/n8/edio.test.ts mirrors
// test/n8/Edio.test.cpp). Every command is a 4-byte framed header ('+', '+'^0xFF, cmd, cmd^0xFF); args are
// little-endian; MIDI / save bytes are delivered by writing to a device address via CMD_MEM_WR. Rides an
// injected SerialTransport (facet-backed in production, a fake in tests), so all the protocol logic is pure
// TS + host-agnostic - the native side keeps ONLY the serial byte transport.

import type { SerialTransport } from "./transport";

// Protocol constants (krikzz Edio) - kept byte-identical to native Edio.hpp.
const CMD_STATUS = 0x10; // connect handshake / status poll
const CMD_MEM_RD = 0x19; // read bytes from a device address
const CMD_MEM_WR = 0x1a; // write bytes to a device address
const CMD_F_DIR_LD = 0xc5; // load a directory into the N8 buffer (sorted)
const CMD_F_DIR_SIZE = 0xc6; // number of records in the loaded directory
const CMD_F_DIR_GET = 0xc8; // pull a range of directory records
const CMD_F_FOPN = 0xc9; // open a file on the SD card
const CMD_F_FRD = 0xca; // read bytes from the open file
const CMD_F_FWR = 0xcc; // write bytes to the open file
const CMD_F_FCLOSE = 0xce; // close the open file

export const ADDR_PRG = 0x000000; // PRG-ROM PSRAM (8 MB) - live code, the same chip the CPU fetches
export const ADDR_CHR = 0x800000; // CHR-ROM PSRAM (8 MB) - live graphics, the same chip the PPU fetches
export const ADDR_SRM = 0x1000000; // cart battery RAM (a game's .srm)
export const ADDR_MENU_CHR = 0xfe0000; // menu CHR (ADDR_CHR 0x800000 + 0x7E0000); screenshot
export const N8_OS_REGION = 0x7e0000; // top of PRG/CHR (0x7E0000..0x800000) is the N8 OS/menu - never patch into it
export const ADDR_SSR = 0x1802000; // save-state sniffer: a running game's live APU/PPU/OAM write-mirror
export const ADDR_FIFO = 0x1810000; // cart FIFO (NES side reads $40F0/$40F1)
export const SIZE_SRM_GAME = 0x10000; // 64 KB - max battery RAM a game uses

const ACK_BLOCK_SIZE = 1024; // fileWrite ack granularity
const RD_BLOCK_SIZE = 4096; // fileRead resp-gated block size (matches edlink)
const TX_BLOCK_SIZE = 8192; // txData chunk (matches native)

// File-open mode flags (FatFs).
export const FA_READ = 0x01;
export const FA_WRITE = 0x02;
export const FA_CREATE_ALWAYS = 0x08;
export const FS_MAKEPATH = 0x80; // create parent dirs if missing

/** One SD-card directory entry (from listDir). */
export interface N8DirEntry {
  name: string;
  size: number;
  isDir: boolean;
}

/** Thrown when the N8 does not answer within the read timeout (no device / wrong port / wedged menu). */
export class N8TimeoutError extends Error {
  constructor(message = "Edio: serial read timeout (no N8 response)") {
    super(message);
    this.name = "N8TimeoutError";
  }
}

const enc = new TextEncoder();
const dec = new TextDecoder();

export class Edio {
  private timeoutMs = 2000; // per-call read timeout, threaded into SerialTransport.read

  constructor(private readonly port: SerialTransport) {}

  // --- connect / status ---

  // The connect handshake: flushInput + CMD_STATUS -> rx16, requiring a 0xA5xx reply (low byte = status,
  // 0 = OK). Uses a short read timeout while probing, then raises it. Returns the status code; throws on a
  // bad or absent reply (no device / wrong port).
  connect(handshakeTimeoutMs = 300): number {
    this.timeoutMs = handshakeTimeoutMs;
    this.port.flushInput();
    const status = this.getStatus();
    this.timeoutMs = 2000; // the reference raises the timeout after a good handshake
    return status;
  }

  getStatus(): number {
    this.txCMD(CMD_STATUS);
    const resp = this.rx16();
    if ((resp & 0xff00) !== 0xa500)
      throw new Error(`Edio: unexpected status response (${hex4(resp & 0xffff)})`);
    return resp & 0xff;
  }

  setReadTimeout(ms: number): void {
    this.timeoutMs = ms;
  }

  flushInput(): void {
    this.port.flushInput();
  }

  // --- device memory ---

  memWR(addr: number, data: Uint8Array): void {
    if (data.length === 0) return;
    this.txCMD(CMD_MEM_WR);
    this.tx32(addr);
    this.tx32(data.length);
    this.tx8(0); // exec flag
    this.txData(data); // fire-and-forget: no status read
  }

  memRD(addr: number, size: number): Uint8Array {
    if (size === 0) return new Uint8Array(0);
    this.txCMD(CMD_MEM_RD);
    this.tx32(addr);
    this.tx32(size);
    this.tx8(0); // exec flag
    return this.rxData(size); // blocks until all bytes arrive (or throws on timeout)
  }

  // Write raw MIDI bytes to the cart FIFO (fire-and-forget).
  fifoWR(data: Uint8Array): void {
    this.memWR(ADDR_FIFO, data);
  }

  // Write a length-prefixed string to the cart FIFO: a 2-byte little-endian length, then the bytes.
  fifoTxString(s: string): void {
    const bytes = enc.encode(s);
    this.fifoWR(new Uint8Array([bytes.length & 0xff, (bytes.length >> 8) & 0xff]));
    this.fifoWR(bytes);
  }

  // --- SD directory / file API ---

  // List an SD-card directory (sorted). Loads the dir into the N8 buffer then pulls every record.
  listDir(path: string): N8DirEntry[] {
    this.txCMD(CMD_F_DIR_LD);
    this.tx8(1); // sorted
    this.txString(path);
    this.checkStatus();

    this.txCMD(CMD_F_DIR_SIZE);
    const count = this.rx16();
    const out: N8DirEntry[] = [];
    if (count <= 0) return out;

    this.txCMD(CMD_F_DIR_GET);
    this.tx16(0); // start index
    this.tx16(count);
    this.tx16(255); // max name length
    for (let i = 0; i < count; i++) {
      const resp = this.rx8();
      if (resp !== 0) throw new Error(`Edio: dir read error 0x${hex2(resp)}`);
      const size = this.rx32();
      this.rx16(); // date (unused)
      this.rx16(); // time (unused)
      const attrib = this.rx8();
      const name = this.rxString();
      out.push({ name, size, isDir: (attrib & 0x10) !== 0 }); // FatFs AM_DIR
    }
    return out;
  }

  fileOpen(path: string, mode: number): void {
    this.txCMD(CMD_F_FOPN);
    this.tx8(mode);
    this.txString(path);
    this.checkStatus();
  }

  fileWrite(data: Uint8Array): void {
    this.txCMD(CMD_F_FWR);
    this.tx32(data.length);
    this.txDataACK(data);
    this.checkStatus();
  }

  fileClose(): void {
    this.txCMD(CMD_F_FCLOSE);
    this.checkStatus();
  }

  // Read `size` bytes from the open file via CMD_F_FRD (resp-gated blocks; the inverse of fileWrite - no
  // trailing status). Pairs with fileOpen(path, FA_READ).
  fileRead(size: number): Uint8Array {
    if (size === 0) return new Uint8Array(0);
    this.txCMD(CMD_F_FRD);
    this.tx32(size);
    return this.rxDataACK(size);
  }

  // Read a whole SD file by path: find its size via listDir, then fileOpen(FA_READ) -> fileRead -> fileClose.
  readFile(path: string): Uint8Array {
    const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
    const dir = slash < 0 ? "" : path.slice(0, slash);
    const name = slash < 0 ? path : path.slice(slash + 1);
    const entry = this.listDir(dir).find((e) => !e.isDir && e.name === name);
    if (!entry) throw new Error(`Edio: file not found on SD: ${path}`);
    this.fileOpen(path, FA_READ);
    const data = this.fileRead(entry.size);
    this.fileClose();
    return data;
  }

  // --- blocking reads (the N8 menu's replies come back this way) ---

  rx8(): number {
    return this.rxData(1)[0];
  }
  rx16(): number {
    const b = this.rxData(2);
    return b[0] | (b[1] << 8);
  }
  rx32(): number {
    const b = this.rxData(4);
    return (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)) >>> 0;
  }

  // Read `size` raw bytes off the serial port (no command frame) - a menu reply the firmware streams after a
  // FIFO command, e.g. N8Menu.vramDump's 2048+16 bytes. (memRD/fileRead can't serve it: each sends its own
  // command first.) Blocks; throws N8TimeoutError on no response.
  readData(size: number): Uint8Array {
    return this.rxData(size);
  }

  // --- framing internals (mirror Edio.cpp) ---

  private txCMD(cmd: number): void {
    this.txData(new Uint8Array([0x2b, 0x2b ^ 0xff, cmd, cmd ^ 0xff]));
  }
  private tx8(v: number): void {
    this.txData(new Uint8Array([v & 0xff]));
  }
  private tx16(v: number): void {
    this.txData(new Uint8Array([v & 0xff, (v >> 8) & 0xff]));
  }
  private tx32(v: number): void {
    this.txData(new Uint8Array([v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff]));
  }

  // Chunk at TX_BLOCK_SIZE like native; a zero-length payload writes nothing (loop does not run).
  private txData(data: Uint8Array): void {
    for (let off = 0; off < data.length; off += TX_BLOCK_SIZE) {
      this.port.write(data.subarray(off, Math.min(off + TX_BLOCK_SIZE, data.length)));
    }
  }

  private txString(s: string): void {
    const bytes = enc.encode(s);
    this.tx16(bytes.length);
    this.txData(bytes);
  }

  // Write in ACK_BLOCK_SIZE blocks, each gated by a 0 ack byte the device sends first.
  private txDataACK(data: Uint8Array): void {
    let off = 0;
    let remaining = data.length;
    while (remaining > 0) {
      const ack = this.rx8();
      if (ack !== 0) throw new Error(`Edio: tx ack error 0x${hex2(ack)}`);
      const block = Math.min(remaining, ACK_BLOCK_SIZE);
      this.txData(data.subarray(off, off + block));
      off += block;
      remaining -= block;
    }
  }

  // Read in RD_BLOCK_SIZE blocks, each preceded by a 0 resp byte the device sends first (inverse of txDataACK).
  private rxDataACK(size: number): Uint8Array {
    const out = new Uint8Array(size);
    let off = 0;
    let remaining = size;
    while (remaining > 0) {
      const resp = this.rx8();
      if (resp !== 0) throw new Error(`Edio: file read error 0x${hex2(resp)}`);
      const block = Math.min(remaining, RD_BLOCK_SIZE);
      out.set(this.rxData(block), off);
      off += block;
      remaining -= block;
    }
    return out;
  }

  // Read exactly `size` bytes, looping over short reads; a zero-length read is a timeout (no response).
  private rxData(size: number): Uint8Array {
    const out = new Uint8Array(size);
    let received = 0;
    while (received < size) {
      const chunk = this.port.read(size - received, this.timeoutMs);
      if (chunk.length === 0) throw new N8TimeoutError();
      out.set(chunk, received);
      received += chunk.length;
    }
    return out;
  }

  private rxString(): string {
    const n = this.rx16();
    return n === 0 ? "" : dec.decode(this.rxData(n));
  }

  private checkStatus(): void {
    const resp = this.getStatus();
    if (resp !== 0) throw new Error(`Edio: operation error 0x${hex2(resp)}`);
  }
}

const hex2 = (v: number): string => v.toString(16).padStart(2, "0");
const hex4 = (v: number): string => v.toString(16).padStart(4, "0");
