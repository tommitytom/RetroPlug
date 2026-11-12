// The N8 a session script opens (@retroplug/n8/device, packages/n8/ts/device.ts: the SDK's connectN8), over a fake RPC
// call: the port it picks and the handshake it runs, the session it closes when the handshake fails, the card and cart
// calls it maps onto the session's, and a closed device refusing every call in its own words, the zero-byte writes the
// client never sends included. The facet end to end, against the fake cart, is test:native's n8-sdk.
import { test, expect } from "@retroplug/testing";
import { openN8Device } from "../device";
import { ADDR_SSR, SNIFFER_REGION_SIZE, type N8Call, type N8Port } from "../index";

interface Reply {
  status: string;
  deviceStatus: number;
  message: string;
  bytes?: Uint8Array;
  value?: number;
  entries?: { name: string; size: number; isDir: boolean; extra?: number }[];
}
const okReply = (more: Partial<Reply> = {}): Reply => ({ status: "ok", deviceStatus: 0, message: "", ...more });

// A host listing `ports` that opens every session as handle 3 and answers each method from `answers` (a reply, or a
// function of the call's params), recording every call.
function fakeHost(
  answers: Record<string, Reply | ((...params: unknown[]) => Reply)>,
  ports: N8Port[] = [{ port: "/dev/ttyACM0", isN8: true }],
): { call: N8Call; sent: unknown[][] } {
  const sent: unknown[][] = [];
  const call: N8Call = (method, ...params) => {
    sent.push([method, ...params]);
    if (method === "n8ListPorts") return ports;
    if (method === "n8Open") return okReply({ value: 3 });
    if (method === "n8Close") return true;
    const a = answers[method];
    if (a === undefined) return okReply();
    return typeof a === "function" ? a(...params) : a;
  };
  return { call, sent };
}

test("it opens the first N8 the host lists, or the port asked for, and runs the handshake", () => {
  const ports = [
    { port: "/dev/ttyS0", isN8: false },
    { port: "/dev/ttyACM3", isN8: true },
  ];
  const found = fakeHost({}, ports);
  expect(openN8Device(found.call).port).toBe("/dev/ttyACM3");
  expect(found.sent).toEqual([["n8ListPorts"], ["n8Open", "/dev/ttyACM3"], ["n8Handshake", 3, 300]]);
  const asked = fakeHost({}, ports);
  expect(openN8Device(asked.call, { serial: "/dev/ttyS0", timeoutMs: 1000 }).port).toBe("/dev/ttyS0");
  expect(asked.sent).toEqual([["n8ListPorts"], ["n8Open", "/dev/ttyS0"], ["n8Handshake", 3, 1000]]);
  expect(() => openN8Device(fakeHost({}, []).call)).toThrow("no Everdrive N8 Pro found (VID:PID 38df:0017)");
});

test("a handshake that is not answered closes the session and throws its failure", () => {
  const unanswered: Reply = {
    status: "timeout",
    deviceStatus: 0,
    message: "Edio: serial read timeout (no N8 response)",
  };
  const { call, sent } = fakeHost({ n8Handshake: unanswered });
  expect(() => openN8Device(call)).toThrow("Edio: serial read timeout (no N8 response)");
  expect(sent.map((s) => s[0])).toEqual(["n8ListPorts", "n8Open", "n8Handshake", "n8Close"]);
});

test("the card, the boot, the sniffer and the FIFO are the session's calls, and a listing is its three fields", () => {
  const { call, sent } = fakeHost({
    n8ListDir: okReply({
      entries: [
        { name: "a.raw", size: 4096, isDir: false, extra: 1 },
        { name: "d", size: 0, isDir: true },
      ],
    }),
    n8ReadFile: okReply({ bytes: new Uint8Array([1, 2]) }),
    n8MemRD: (_h, _addr, size) => okReply({ bytes: new Uint8Array(size as number).fill(7) }),
    n8BootRom: okReply({ value: 30 }),
  });
  const dev = openN8Device(call);
  expect(dev.listDir("/")).toEqual([
    { name: "a.raw", size: 4096, isDir: false },
    { name: "d", size: 0, isDir: true },
  ]);
  dev.listDir("/clips");
  expect(Array.from(dev.readFile("/clip.raw"))).toEqual([1, 2]);
  dev.writeFile("/clips/b.raw", new Uint8Array([3]));
  dev.makeDir("/clips");
  dev.deleteFile("/clips/b.raw");
  expect(dev.sniffRaw().length).toBe(SNIFFER_REGION_SIZE);
  dev.writeFifo(new Uint8Array([0x90, 0x3c, 0x7f]));
  expect(dev.load({ romBytes: new Uint8Array(16), romName: "clip.bin" })).toEqual({
    bootPath: "usb-games/clip.bin",
    mapIndex: 30,
  });
  expect(sent.slice(3)).toEqual([
    ["n8ListDir", 3, ""], // "/" is the root, which the card names ""
    ["n8ListDir", 3, "/clips"],
    ["n8ReadFile", 3, "/clip.raw"],
    ["n8WriteFile", 3, "/clips/b.raw", new Uint8Array([3])],
    ["n8DirMake", 3, "/clips"],
    ["n8FileDelete", 3, "/clips/b.raw"],
    ["n8MemRD", 3, ADDR_SSR, SNIFFER_REGION_SIZE],
    ["n8FifoWR", 3, new Uint8Array([0x90, 0x3c, 0x7f])],
    ["n8BootRom", 3, "usb-games/clip.bin", new Uint8Array(16), new Uint8Array(0), true],
  ]);
});

test("every call after close throws the device's own words, a zero-byte write included, and sends nothing", () => {
  const { call, sent } = fakeHost({});
  const dev = openN8Device(call);
  dev.close();
  dev.close(); // once is enough: the second sends nothing
  const calls: [string, () => unknown][] = [
    ["load", () => dev.load({ sdPath: "games/a.bin" })],
    ["listDir", () => dev.listDir("/")],
    ["readFile", () => dev.readFile("/a")],
    ["writeFile", () => dev.writeFile("/a", new Uint8Array(0))],
    ["makeDir", () => dev.makeDir("/d")],
    ["deleteFile", () => dev.deleteFile("/a")],
    ["sniffRaw", () => dev.sniffRaw()],
    ["writeFifo", () => dev.writeFifo(new Uint8Array(0))],
  ];
  for (const [name, f] of calls) expect(f).toThrow(`N8Device.${name}: the device is closed`);
  expect(sent.map((s) => s[0])).toEqual(["n8ListPorts", "n8Open", "n8Handshake", "n8Close"]);
});

test("a close that throws on the failed-handshake path leaves the handshake's error the one thrown", () => {
  const unanswered: Reply = { status: "timeout", deviceStatus: 0, message: "Edio: serial read timeout" };
  const { call } = fakeHost({ n8Handshake: unanswered });
  const gone: N8Call = (method, ...params) => {
    if (method === "n8Close") throw new Error("channel gone");
    return call(method, ...params);
  };
  expect(() => openN8Device(gone)).toThrow("Edio: serial read timeout");
});

test("a close the host refuses can be retried: the device is closed once the host has released it", () => {
  const { call, sent } = fakeHost({});
  let refuse = true;
  const flaky: N8Call = (method, ...params) => {
    if (method === "n8Close" && refuse) {
      refuse = false;
      throw new Error("channel gone");
    }
    return call(method, ...params);
  };
  const dev = openN8Device(flaky);
  expect(() => dev.close()).toThrow("channel gone");
  dev.close(); // the host never released the session, so this one does
  expect(sent.filter((s) => s[0] === "n8Close").length).toBe(1);
  expect(() => dev.listDir("/")).toThrow("N8Device.listDir: the device is closed");
});
