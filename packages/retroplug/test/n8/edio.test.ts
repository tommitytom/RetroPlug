// Guards the TS Everdrive N8 Pro protocol framing (src/n8/edio.ts) without hardware: a FakeSerialPort
// captures every byte Edio writes, so we assert fifoWR / the file API emit the exact krikzz command stream
// and the connect handshake accepts / rejects the 0xA5 status word. The TS twin of the native gtest
// packages/native/test/n8/Edio.test.cpp - the same vectors, so both stay byte-identical.
import { test, expect } from "../../testing/harness";
import { Edio, N8TimeoutError, ADDR_SRM, FA_WRITE, FA_CREATE_ALWAYS, FS_MAKEPATH } from "../../src/n8/edio";
import { FakeSerialPort } from "../../src/n8/fakeSerial";

const u32le = (n: number): number[] => [n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, (n >>> 24) & 0xff];

// The CMD_MEM_WR frame targeting ADDR_FIFO for a payload (what fifoWR emits). Mirrors memWrFifo in the gtest.
const memWrFifo = (payload: number[]): number[] => [
  0x2b, 0xd4, 0x1a, 0xe5, // frame '+', 0xD4, CMD_MEM_WR, ^0xFF
  0x00, 0x00, 0x81, 0x01, // ADDR_FIFO = 0x01810000, little-endian
  ...u32le(payload.length),
  0x00, // exec flag
  ...payload,
];
const CMD_STATUS_FRAME = [0x2b, 0xd4, 0x10, 0xef]; // 0x10 ^ 0xFF = 0xEF

test("fifoWR emits the exact krikzz CMD_MEM_WR frame to ADDR_FIFO", () => {
  const port = new FakeSerialPort();
  new Edio(port).fifoWR(new Uint8Array([0x90, 0x3c, 0x7f])); // note-on, middle C, velocity 127
  expect(port.written).toEqual(memWrFifo([0x90, 0x3c, 0x7f]));
});

test("fifoWR on empty input writes nothing", () => {
  const port = new FakeSerialPort();
  new Edio(port).fifoWR(new Uint8Array([]));
  expect(port.written).toEqual([]);
});

test("connect flushes, sends CMD_STATUS, and accepts a 0xA5xx reply", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa500); // high byte 0xA5, status 0 = OK
  const edio = new Edio(port);
  expect(edio.connect()).toBe(0);
  expect(port.flushed).toBe(true);
  expect(port.written).toEqual(CMD_STATUS_FRAME);
});

test("connect surfaces the low status byte", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa5c3); // high byte OK, status code 0xC3
  expect(new Edio(port).connect()).toBe(0xc3);
});

test("connect throws on a non-0xA5 status word", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0x1234); // wrong high byte
  const edio = new Edio(port);
  expect(() => edio.connect()).toThrow();
});

test("connect throws (timeout) when the device does not answer", () => {
  const port = new FakeSerialPort(); // no queued reply => read returns 0 => timeout
  const edio = new Edio(port);
  let err: unknown;
  try {
    edio.connect();
  } catch (e) {
    err = e;
  }
  expect(err instanceof N8TimeoutError).toBe(true);
});

test("fifoTxString emits a 2-byte LE length then the bytes, each as a FIFO write", () => {
  const port = new FakeSerialPort();
  new Edio(port).fifoTxString("ab");
  expect(port.written).toEqual([...memWrFifo([0x02, 0x00]), ...memWrFifo([0x61, 0x62])]);
});

test("fileOpen sends CMD_F_FOPN + mode + length-prefixed path, then polls status", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa500); // checkStatus poll -> ok
  new Edio(port).fileOpen("ab", FA_WRITE | FA_CREATE_ALWAYS | FS_MAKEPATH);
  expect(port.written).toEqual([
    0x2b, 0xd4, 0xc9, 0x36, // frame CMD_F_FOPN (0xC9 ^ 0xFF = 0x36)
    0x8a, // mode = FA_WRITE|FA_CREATE_ALWAYS|FS_MAKEPATH
    0x02, 0x00, // path length = 2 (tx16)
    0x61, 0x62, // 'a', 'b'
    ...CMD_STATUS_FRAME,
  ]);
});

test("fileWrite sends CMD_F_FWR + length, one ack-gated block, then polls status", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x00); // txDataACK: ack byte for the first (only) block
  port.queueStatus(0xa500); // checkStatus poll -> ok
  new Edio(port).fileWrite(new Uint8Array([0xde, 0xad]));
  expect(port.written).toEqual([
    0x2b, 0xd4, 0xcc, 0x33, // frame CMD_F_FWR (0xCC ^ 0xFF = 0x33)
    0x02, 0x00, 0x00, 0x00, // length = 2 (tx32)
    0xde, 0xad, // the block (after the ack byte was read)
    ...CMD_STATUS_FRAME,
  ]);
});

test("fileClose sends CMD_F_FCLOSE then polls status", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa500);
  new Edio(port).fileClose();
  expect(port.written).toEqual([0x2b, 0xd4, 0xce, 0x31, ...CMD_STATUS_FRAME]); // 0xCE ^ 0xFF = 0x31
});

test("memRD sends CMD_MEM_RD to the address and returns the bytes read", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0xde, 0xad, 0xbe, 0xef);
  const out = new Edio(port).memRD(ADDR_SRM, 4);
  expect(Array.from(out)).toEqual([0xde, 0xad, 0xbe, 0xef]);
  expect(port.written).toEqual([
    0x2b, 0xd4, 0x19, 0xe6, // frame CMD_MEM_RD (0x19 ^ 0xFF = 0xE6)
    ...u32le(ADDR_SRM), // 0x00, 0x00, 0x00, 0x01
    0x04, 0x00, 0x00, 0x00, // size = 4
    0x00, // exec flag
  ]);
});

test("listDir decodes the sorted directory records (file + subdir)", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa500); // DIR_LD checkStatus -> ok
  port.queueBytes(0x02, 0x00); // DIR_SIZE -> 2 records
  // record 0: dir "sub" (attrib AM_DIR 0x10, size 0)
  port.queueBytes(0x00, /*size*/ 0x00, 0x00, 0x00, 0x00, /*date*/ 0x00, 0x00, /*time*/ 0x00, 0x00, /*attrib*/ 0x10, /*len*/ 0x03, 0x00, 0x73, 0x75, 0x62);
  // record 1: file "ab" (size 16)
  port.queueBytes(0x00, /*size*/ 0x10, 0x00, 0x00, 0x00, /*date*/ 0x00, 0x00, /*time*/ 0x00, 0x00, /*attrib*/ 0x00, /*len*/ 0x02, 0x00, 0x61, 0x62);

  const entries = new Edio(port).listDir("x");
  expect(entries).toEqual([
    { name: "sub", size: 0, isDir: true },
    { name: "ab", size: 16, isDir: false },
  ]);
  expect(port.written.slice(0, 4)).toEqual([0x2b, 0xd4, 0xc5, 0x3a]); // opened with CMD_F_DIR_LD (0xC5 ^ 0xFF = 0x3A)
});
