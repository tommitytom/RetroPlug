// Guards the TS Everdrive N8 Pro protocol framing (src/n8/edio.ts) without hardware. The write-vector cases
// live in the SHARED golden (edio-golden.json), which the native gtest (packages/native/test/n8/Edio.test.cpp)
// asserts against too - so any framing change in either impl fails the other's test until both + the golden
// agree. This file additionally covers the TS-side semantics the golden doesn't (return values, throws, the
// N8TimeoutError type, directory decode).
import { test, expect } from "../../testing/harness";
import { Edio, N8TimeoutError, ADDR_SRM } from "../../src/n8/edio";
import { FakeSerialPort } from "../../src/n8/fakeSerial";
import goldenJson from "./edio-golden.json";

interface GoldenCase {
  id: string;
  op: string;
  args?: { bytes?: string; str?: string; path?: string; mode?: number; addr?: number; size?: number };
  reads?: string;
  writes: string;
}
const golden = goldenJson as unknown as { cases: GoldenCase[] };

const fromHex = (h: string): number[] => {
  const out: number[] = [];
  for (let i = 0; i + 1 < h.length; i += 2) out.push(parseInt(h.slice(i, i + 2), 16));
  return out;
};

// Drive one golden case against a fresh Edio and return the bytes it wrote. Reads are pre-queued so a
// status/ack-polling op completes. Throws on an unknown op so a new golden op can't silently pass.
function driveGolden(c: GoldenCase): number[] {
  const port = new FakeSerialPort();
  if (c.reads) port.queueBytes(...fromHex(c.reads));
  const edio = new Edio(port);
  const a = c.args ?? {};
  switch (c.op) {
    case "fifoWR": edio.fifoWR(new Uint8Array(fromHex(a.bytes ?? ""))); break;
    case "fifoTxString": edio.fifoTxString(a.str ?? ""); break;
    case "connect": edio.connect(); break;
    case "fileOpen": edio.fileOpen(a.path ?? "", a.mode ?? 0); break;
    case "fileWrite": edio.fileWrite(new Uint8Array(fromHex(a.bytes ?? ""))); break;
    case "fileClose": edio.fileClose(); break;
    case "memRD": edio.memRD(a.addr ?? 0, a.size ?? 0); break;
    case "fileRead": edio.fileRead(a.size ?? 0); break;
    default: throw new Error(`edio golden: unknown op '${c.op}'`);
  }
  return port.written;
}

for (const c of golden.cases) {
  test(`edio framing (shared golden): ${c.id}`, () => {
    expect(driveGolden(c)).toEqual(fromHex(c.writes));
  });
}

// --- TS-side semantics (not framing; stays per-language) --------------------------------------------

test("connect flushes and returns 0 on a 0xA500 (OK) reply", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa500);
  expect(new Edio(port).connect()).toBe(0);
  expect(port.flushed).toBe(true);
});

test("connect surfaces the low status byte", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0xa5c3); // high byte OK, status code 0xC3
  expect(new Edio(port).connect()).toBe(0xc3);
});

test("connect throws on a non-0xA5 status word", () => {
  const port = new FakeSerialPort();
  port.queueStatus(0x1234); // wrong high byte
  expect(() => new Edio(port).connect()).toThrow();
});

test("connect throws N8TimeoutError when the device does not answer", () => {
  const port = new FakeSerialPort(); // no queued reply => read returns 0 => timeout
  let err: unknown;
  try {
    new Edio(port).connect();
  } catch (e) {
    err = e;
  }
  expect(err instanceof N8TimeoutError).toBe(true);
});

test("memRD returns the bytes read from the address", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0xde, 0xad, 0xbe, 0xef);
  expect(Array.from(new Edio(port).memRD(ADDR_SRM, 4))).toEqual([0xde, 0xad, 0xbe, 0xef]);
});

test("fileRead returns the bytes after the per-block resp byte", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x00, 0xde, 0xad, 0xbe, 0xef); // resp 0 (ok), then 4 data bytes
  expect(Array.from(new Edio(port).fileRead(4))).toEqual([0xde, 0xad, 0xbe, 0xef]);
});

test("fileRead loops resp-gated blocks over RD_BLOCK_SIZE (4096)", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x00, ...new Array(4096).fill(0xaa)); // block 1: resp + 4096 bytes
  port.queueBytes(0x00, 0xbb, 0xbb, 0xbb, 0xbb); //         block 2: resp + 4 bytes
  const out = new Edio(port).fileRead(4100);
  expect(out.length).toBe(4100);
  expect(out[0]).toBe(0xaa);
  expect(out[4095]).toBe(0xaa);
  expect(Array.from(out.slice(4096))).toEqual([0xbb, 0xbb, 0xbb, 0xbb]);
});

test("fileRead throws on a non-zero per-block resp", () => {
  const port = new FakeSerialPort();
  port.queueBytes(0x04); // FAT_NO_FILE-ish error resp
  expect(() => new Edio(port).fileRead(4)).toThrow();
});

test("readFile finds the size via listDir, then reads the whole file", () => {
  const port = new FakeSerialPort();
  // listDir("d"): DIR_LD checkStatus ok, DIR_SIZE = 1, one record: file "f" size 4
  port.queueStatus(0xa500);
  port.queueBytes(0x01, 0x00);
  port.queueBytes(0x00, /*size*/ 0x04, 0x00, 0x00, 0x00, /*date*/ 0x00, 0x00, /*time*/ 0x00, 0x00, /*attrib*/ 0x00, /*len*/ 0x01, 0x00, /*"f"*/ 0x66);
  // fileOpen("d/f", FA_READ) checkStatus ok; fileRead(4) resp + data; fileClose checkStatus ok
  port.queueStatus(0xa500);
  port.queueBytes(0x00, 0xde, 0xad, 0xbe, 0xef);
  port.queueStatus(0xa500);
  expect(Array.from(new Edio(port).readFile("d/f"))).toEqual([0xde, 0xad, 0xbe, 0xef]);
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
