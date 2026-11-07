// The N8 pack's client, its session half (@retroplug/n8, packages/n8/ts/index.ts), over a fake RPC call: openN8 opens
// the port by name and every call of the N8 it answers sends the facet's method on that session's handle
// (packages/n8/native/src/n8/desktop/N8Rpc.hpp), a reply's failure throws its message (an N8TimeoutError when the N8
// did not answer, as the TS client it replaced threw), load() is the host's bootRom with its options checked here, and
// the load helpers' direct writes are a memWR and a read-back memRD. The protocol itself is held in C++
// (retroplug-n8-test), and the facet end to end in test:native (n8-session); this is the TS half alone, in the mock
// tier.
import { test, expect } from "@retroplug/testing";
import {
  createN8Client,
  N8TimeoutError,
  ADDR_CFG,
  ADDR_PRG,
  ADDR_SRM,
  ADDR_CHR,
  N8_OS_REGION,
  SIZE_SRM_GAME,
  assertGameRegion,
  type N8Call,
} from "../index";

interface Reply {
  status: string;
  deviceStatus: number;
  message: string;
  bytes?: Uint8Array;
  value?: number;
  entries?: { name: string; size: number; isDir: boolean }[];
}
const okReply = (more: Partial<Reply> = {}): Reply => ({ status: "ok", deviceStatus: 0, message: "", ...more });

// A host that answers each method from `answers` (a reply, or a function of the call's params), recording every call.
function fakeHost(answers: Record<string, Reply | ((...params: unknown[]) => Reply)>): {
  call: N8Call;
  sent: unknown[][];
} {
  const sent: unknown[][] = [];
  const call: N8Call = (method, ...params) => {
    sent.push([method, ...params]);
    if (method === "n8Open") return okReply({ value: 7 });
    if (method === "n8Close") return true;
    const a = answers[method];
    if (a === undefined) return okReply();
    return typeof a === "function" ? a(...params) : a;
  };
  return { call, sent };
}

test("openN8 opens the port by name, and each call sends the facet's method on its handle", () => {
  const { call, sent } = fakeHost({
    n8Handshake: okReply({ value: 0 }),
    n8SysInfo: okReply({ bytes: new Uint8Array(64).fill(1) }),
    n8MemRD: okReply({ bytes: new Uint8Array([1, 2, 3]) }),
    n8FreeSpace: okReply({ value: 4096 }),
    n8ListDir: okReply({ entries: [{ name: "a.nes", size: 3, isDir: false }] }),
    n8ReadFile: okReply({ bytes: new Uint8Array([9, 8]) }),
    n8MenuInstall: okReply({ value: 12 }),
    n8MenuVramDump: okReply({ bytes: new Uint8Array(2064).map((_, i) => i & 0xff) }),
    n8ReadData: (size) => okReply({ bytes: new Uint8Array(size as number).fill(0x5a) }),
  });
  const n8 = createN8Client(call).openN8("/dev/ttyACM0");
  expect(n8.port).toBe("/dev/ttyACM0");
  expect(n8.connect()).toBe(0);
  expect(n8.edio.sysInfo().length).toBe(64);
  expect(Array.from(n8.edio.memRD(ADDR_SRM, 3))).toEqual([1, 2, 3]);
  expect(n8.edio.freeSpace()).toBe(4096);
  n8.edio.memWR(ADDR_PRG, new Uint8Array([4]));
  n8.edio.fifoWR(new Uint8Array([0x90, 0x3c, 0x7f]));
  n8.edio.fifoWR(new Uint8Array(0)); // nothing to send: no call
  expect(n8.listDir("usb-games")).toEqual([{ name: "a.nes", size: 3, isDir: false }]);
  expect(Array.from(n8.readFile("d/x"))).toEqual([9, 8]);
  n8.writeFile("d/y", new Uint8Array([5]));
  n8.edio.dirMake("d");
  n8.edio.fileDelete("d/y");
  expect(n8.edio.rx16()).toBe(0x5a5a);
  n8.menu.test();
  expect(n8.menu.appInstall("usb-games/a.nes")).toBe(12);
  n8.menu.appStart();
  const shot = n8.menu.vramDump();
  expect(shot.vram.length).toBe(2048);
  expect(shot.palette.length).toBe(16);
  expect(shot.palette[0]).toBe(2048 & 0xff);
  n8.close();
  const methods = sent.map((s) => s[0]);
  expect(methods).toEqual([
    "n8Open", "n8Handshake", "n8SysInfo", "n8MemRD", "n8FreeSpace", "n8MemWR", "n8FifoWR", "n8ListDir",
    "n8ReadFile", "n8WriteFile", "n8DirMake", "n8FileDelete", "n8ReadData", "n8MenuTest", "n8MenuInstall",
    "n8MenuStart", "n8MenuVramDump", "n8Close",
  ]);
  expect(sent[0]).toEqual(["n8Open", "/dev/ttyACM0"]);
  expect(sent[1]).toEqual(["n8Handshake", 7, 300]); // today's handshake timeout, on the session's handle
  expect(sent[3]).toEqual(["n8MemRD", 7, ADDR_SRM, 3]);
  expect(sent[12]).toEqual(["n8ReadData", 7, 2, 0]);
  for (const s of sent.slice(1)) expect(s[1]).toBe(7);
});

test("a failed call throws the reply's message, and an N8TimeoutError when the N8 did not answer", () => {
  const failing = (status: string, message: string, deviceStatus = 0): Reply => ({ status, deviceStatus, message });
  const cases: [string, Reply][] = [
    ["timeout", failing("timeout", "Edio: serial read timeout (no N8 response)")],
    ["device", failing("device", "Edio: operation error 0x04", 4)],
    ["badReply", failing("badReply", "N8 menu: unexpected test response 0x78 (is the menu running?)")],
    ["linkDown", failing("linkDown", "no open N8 session (handle 7)")],
    ["notFound", failing("notFound", "Edio: file not found on SD: d/x")],
    ["tooLarge", failing("tooLarge", "Edio: a string longer than 65535 bytes (x)")],
    ["cancelled", failing("cancelled", "Edio: cancelled before the install")],
  ];
  for (const [status, reply] of cases) {
    const n8 = createN8Client(fakeHost({ n8MenuTest: reply }).call).openN8("p");
    let thrown: unknown;
    try {
      n8.menu.test();
    } catch (e) {
      thrown = e;
    }
    expect(thrown instanceof Error).toBe(true);
    expect((thrown as Error).message).toBe(reply.message);
    expect(thrown instanceof N8TimeoutError).toBe(status === "timeout");
  }
  // The port's own refusal, as the serial client it replaced threw it.
  const refused: N8Call = (method, ...params) =>
    method === "n8Open" ? failing("linkDown", `cannot open serial port: ${params[0]}`) : okReply();
  expect(() => createN8Client(refused).openN8("/dev/ttyACM9")).toThrow("cannot open serial port: /dev/ttyACM9");
  // A host with no N8 facet at all answers no ports, whether its call answers nothing or throws "Method not found".
  expect(createN8Client(() => undefined).listPorts()).toEqual([]);
  const noFacet: N8Call = (method) => {
    throw new Error(`rpc ${method}: [-32601] Method not found`);
  };
  expect(createN8Client(noFacet).listPorts()).toEqual([]);
  const broken: N8Call = () => {
    throw new Error("rpc n8ListPorts: [-32603] the port lister failed");
  };
  expect(() => createN8Client(broken).listPorts()).toThrow("the port lister failed");
});

test("load boots through the host's bootRom: an upload to usb-games, or the SD path as it is", () => {
  const { call, sent } = fakeHost({ n8BootRom: okReply({ value: 69 }) });
  const n8 = createN8Client(call).openN8("p");
  const rom = new Uint8Array([0x4e, 0x45, 0x53, 0x1a]);
  const srm = new Uint8Array([1, 2]);
  expect(n8.load({ romBytes: rom, romName: "song.nes", srm })).toEqual({
    bootPath: "usb-games/song.nes",
    mapIndex: 69,
  });
  expect(sent[1]).toEqual(["n8BootRom", 7, "usb-games/song.nes", rom, srm, true]);
  expect(n8.load({ sdPath: "games/held.nes", romBytes: rom })).toEqual({ bootPath: "games/held.nes", mapIndex: 69 });
  expect(sent[2]).toEqual(["n8BootRom", 7, "games/held.nes", new Uint8Array(0), new Uint8Array(0), false]);
  // Neither a ROM nor an SD path: refused before anything is sent, in loadRom's words.
  expect(() => n8.load({})).toThrow("loadRom: pass romBytes + romName to upload, or sdPath to boot an existing SD ROM");
  expect(sent.length).toBe(3);
});

test("the direct writes are a memWR and a read-back memRD, and fail on a mismatch", () => {
  let mem = new Uint8Array(0);
  const echo = fakeHost({
    n8MemWR: (_h, _addr, data) => {
      mem = new Uint8Array(data as Uint8Array);
      return okReply();
    },
    n8MemRD: (_h, _addr, size) => okReply({ bytes: mem.slice(0, size as number) }),
  });
  const n8 = createN8Client(echo.call).openN8("p");
  const save = new Uint8Array(SIZE_SRM_GAME + 10).map((_, i) => i & 0xff);
  expect(n8.writeSramDirect(save)).toBe(SIZE_SRM_GAME); // capped at the game region
  expect(echo.sent[1]).toEqual(["n8MemWR", 7, ADDR_SRM, save.subarray(0, SIZE_SRM_GAME)]);
  expect(echo.sent[2]).toEqual(["n8MemRD", 7, ADDR_SRM, SIZE_SRM_GAME]);
  expect(n8.writeMemDirect(ADDR_CHR + 4, new Uint8Array([7, 7]))).toBe(2);
  expect(n8.writeMemDirect(ADDR_CHR, new Uint8Array(0))).toBe(0); // nothing sent
  expect(echo.sent.length).toBe(5);
  expect(Array.from(n8.dumpSram().subarray(0, 2))).toEqual([7, 7]);

  const flipped = fakeHost({
    n8MemRD: (_h, _a, size) => okReply({ bytes: new Uint8Array(size as number).fill(0xff) }),
  });
  const bad = createN8Client(flipped.call).openN8("p");
  expect(() => bad.writeSramDirect(new Uint8Array([1, 2]))).toThrow("cart SRAM verify failed (readback != save)");
  expect(() => bad.writeMemDirect(ADDR_PRG + 0x10, new Uint8Array([1]))).toThrow("device write verify failed at 0x10");
});

test("assertGameRegion allows the game region and blocks the N8 OS region", () => {
  assertGameRegion(0, 16);
  assertGameRegion(N8_OS_REGION - 16, 16);
  expect(() => assertGameRegion(N8_OS_REGION - 8, 16)).toThrow("runs into the N8 OS region");
  expect(() => assertGameRegion(-1, 1)).toThrow("non-negative integer");
  expect(() => assertGameRegion(1.5, 1)).toThrow("non-negative integer");
});

test("mapConfig reads the FPGA config block and decodes it", () => {
  const cfg = new Uint8Array(16);
  cfg[0] = 30; // map_idx 30
  const { call, sent } = fakeHost({ n8MemRD: okReply({ bytes: cfg }) });
  const n8 = createN8Client(call).openN8("p");
  expect(n8.mapConfig().mapIdx).toBe(30);
  expect(sent[1]).toEqual(["n8MemRD", 7, ADDR_CFG, 16]);
});
