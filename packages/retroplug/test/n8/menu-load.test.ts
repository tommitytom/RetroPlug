// Guards the TS N8 menu command channel (src/n8/n8Menu.ts) + the ROM/save orchestration (src/n8/n8Load.ts)
// against a FakeSerialPort. The menu cases mirror the native gtest (packages/native/test/n8/Edio.test.cpp)
// byte-for-byte; the load cases exercise loadRom / writeSramDirect over the fake.
import { test, expect } from "../../testing/harness";
import { Edio, ADDR_SRM, ADDR_CHR, N8_OS_REGION } from "../../src/n8/edio";
import { N8Menu } from "../../src/n8/n8Menu";
import { loadRom, writeSramDirect, writeMemDirect, assertGameRegion } from "../../src/n8/n8Load";
import { FakeSerialPort } from "../../src/n8/fakeSerial";

const u32le = (n: number): number[] => [n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, (n >>> 24) & 0xff];
const memWrFifo = (payload: number[]): number[] => [
  0x2b, 0xd4, 0x1a, 0xe5, 0x00, 0x00, 0x81, 0x01, ...u32le(payload.length), 0x00, ...payload,
];
const endsWith = (haystack: number[], tail: number[]): boolean => {
  if (tail.length > haystack.length) return false;
  const off = haystack.length - tail.length;
  return tail.every((b, i) => haystack[off + i] === b);
};

// --- N8Menu (mirrors the native gtest) ------------------------------------------------------------------

test("N8Menu.test sends '*t' to the FIFO and accepts 'k'", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x6b); // 'k'
  new N8Menu(new Edio(port)).test();
  expect(port.written).toEqual(memWrFifo([0x2a, 0x74])); // '*', 't'
});

test("N8Menu.test throws on a non-'k' reply", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x78); // 'x'
  const menu = new N8Menu(new Edio(port));
  expect(() => menu.test()).toThrow();
});

test("N8Menu.appInstall sends '*n' + length-prefixed path, returns the map index", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x00, 0x07, 0x00); // status ok, map index 7 (LE)
  const idx = new N8Menu(new Edio(port)).appInstall("x");
  expect(idx).toBe(7);
  expect(port.written).toEqual([...memWrFifo([0x2a, 0x6e]), ...memWrFifo([0x01, 0x00]), ...memWrFifo([0x78])]);
});

test("N8Menu.appInstall throws on a non-zero install status", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x05); // FR_NO_PATH
  const menu = new N8Menu(new Edio(port));
  expect(() => menu.appInstall("bad/path.nes")).toThrow();
});

test("N8Menu.appInstall surfaces the 0x44 out-of-memory guidance", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x44); // ERR_OUT_OF_MEMORY
  const menu = new N8Menu(new Edio(port));
  expect(() => menu.appInstall("x")).toThrow("out of memory");
});

test("N8Menu.appStart sends '*s'", () => {
  const port = new FakeSerialPort();
  new N8Menu(new Edio(port)).appStart();
  expect(port.written).toEqual(memWrFifo([0x2a, 0x73])); // '*', 's'
});

test("N8Menu.vramDump sends '*v' and splits the 2048+16 reply into vram + palette", () => {
  const port = new FakeSerialPort();
  const reply: number[] = [];
  for (let i = 0; i < 2048; i++) reply.push(i & 0xff); // vram
  for (let i = 0; i < 16; i++) reply.push(0xa0 + i); // palette
  port.queueBytes(...reply);
  const { vram, palette } = new N8Menu(new Edio(port)).vramDump();
  expect(vram.length).toBe(2048);
  expect(palette.length).toBe(16);
  expect(vram[0]).toBe(0);
  expect(vram[2047]).toBe(2047 & 0xff);
  expect(Array.from(palette)).toEqual([0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf]);
  expect(port.written).toEqual(memWrFifo([0x2a, 0x76])); // '*', 'v'
});

// --- loadRom orchestration ------------------------------------------------------------------------------

test("loadRom (sdPath) handshakes, installs, and boots - '*s' is written last", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x6b); // test -> 'k'
  port.queueBytes(0x00, 0x03, 0x00); // appInstall status ok + map index 3
  const res = loadRom(new Edio(port), { sdPath: "usb-games/mgb.gb" });
  expect(res.mapIndex).toBe(3);
  expect(res.bootPath).toBe("usb-games/mgb.gb");
  expect(endsWith(port.written, memWrFifo([0x2a, 0x73]))).toBe(true); // booted with '*s'
});

test("loadRom writes the save to the gamedata slot before booting", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x6b); // test -> 'k'
  port.queueStatus(0xa500); // gamedata fileOpen checkStatus
  port.queueBytes(0x00); // fileWrite ack
  port.queueStatus(0xa500); // fileWrite checkStatus
  port.queueStatus(0xa500); // fileClose checkStatus
  port.queueBytes(0x00, 0x02, 0x00); // appInstall status ok + map index 2
  const res = loadRom(new Edio(port), { sdPath: "usb-games/risa.nes", srm: new Uint8Array([0xaa, 0xbb]) });
  expect(res.mapIndex).toBe(2);
  // The gamedata path bytes must have crossed the wire (EDN8/gamedata/risa.nes/bram.srm).
  const wire = String.fromCharCode(...port.written.filter((b) => b >= 0x20 && b < 0x7f));
  expect(wire.includes("EDN8/gamedata/risa.nes/bram.srm")).toBe(true);
});

// --- writeSramDirect ------------------------------------------------------------------------------------

test("writeSramDirect writes to ADDR_SRM and verifies the readback", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x11, 0x22, 0x33, 0x44); // memRD verify -> identical
  const n = writeSramDirect(new Edio(port), new Uint8Array([0x11, 0x22, 0x33, 0x44]));
  expect(n).toBe(4);
  // First write is the CMD_MEM_WR frame to ADDR_SRM.
  expect(port.written.slice(0, 12)).toEqual([0x2b, 0xd4, 0x1a, 0xe5, ...u32le(ADDR_SRM), ...u32le(4)]);
});

test("writeSramDirect throws when the readback differs", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x11, 0x22, 0x33, 0xff); // last byte differs
  const edio = new Edio(port);
  expect(() => writeSramDirect(edio, new Uint8Array([0x11, 0x22, 0x33, 0x44]))).toThrow("verify failed");
});

// --- writeMemDirect (live CHR/PRG hot-patch) + assertGameRegion ------------------------------------------

test("writeMemDirect writes a block to a device address and verifies the readback", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0xde, 0xad, 0xbe, 0xef); // memRD verify -> identical
  const n = writeMemDirect(new Edio(port), ADDR_CHR + 0x40, new Uint8Array([0xde, 0xad, 0xbe, 0xef]));
  expect(n).toBe(4);
  // First frame is the CMD_MEM_WR (0x1a) to ADDR_CHR + 0x40.
  expect(port.written.slice(0, 12)).toEqual([0x2b, 0xd4, 0x1a, 0xe5, ...u32le(ADDR_CHR + 0x40), ...u32le(4)]);
});

test("writeMemDirect throws when the readback differs", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0xde, 0xad, 0xbe, 0x00); // last byte differs
  expect(() => writeMemDirect(new Edio(port), ADDR_CHR, new Uint8Array([0xde, 0xad, 0xbe, 0xef]))).toThrow("verify failed");
});

test("assertGameRegion allows the game region and blocks the N8 OS region", () => {
  assertGameRegion(0, 8192); // in-range: a throw here fails the test
  assertGameRegion(N8_OS_REGION - 4, 4); // ends exactly at the OS boundary - allowed
  expect(() => assertGameRegion(N8_OS_REGION - 4, 8)).toThrow(); // spills into the OS region
  expect(() => assertGameRegion(N8_OS_REGION, 1)).toThrow();
  expect(() => assertGameRegion(-1, 1)).toThrow();
});
