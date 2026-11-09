// The CLI's N8 sessions through the real facet, to a fake cart (docs/modular-graph-phase-e2-design.md section 7): this
// host (retroplug-host, packages/native/src/main.cpp) starts the real N8 provider with FakeN8's ports (the model of the
// cart the pack's C++ tests drive, packages/n8/native/test/FakeN8.hpp), so `n8-load` runs here exactly as on the CLI,
// its sessions opened through @retroplug/n8's createN8Client and the protocol run in the host (n8/Edio.hpp): the ports
// it auto-detects, the device info, mkdir, a file put, listed, got back and removed, a ROM booted with its battery
// through the menu, the running cart's config block read back, and the failures the CLI prints in the TS client's
// words. Each run opens and closes its own session, as each CLI process does.
import { test, expect } from "@retroplug/testing";
import { bootSession } from "../cli/session";
import { n8LoadTool } from "@retroplug/n8/cli";
import { cliHost } from "../cli/cliHost";
import { makeCall } from "../src/rpcClient";
import { createN8Client, N8TimeoutError, ADDR_SSR, decodeSniffer, SNIFFER_REGION_SIZE } from "@retroplug/n8";

const s = bootSession();

// One run of the tool, its console output caught.
function n8Load(...args: string[]): string[] {
  const lines: string[] = [];
  const log = console.log;
  console.log = (...parts: unknown[]) => void lines.push(parts.map(String).join(" "));
  try {
    n8LoadTool.run(cliHost(s), args);
  } finally {
    console.log = log;
  }
  return lines;
}

const TMP = "/tmp/rp-n8-session-test";
const local = (name: string): string => `${TMP}-${name}`;

test("the facet lists the fake cart's port as an N8, and a session opens on it", () => {
  const client = createN8Client(makeCall());
  expect(client.listPorts()).toEqual([{ port: "fake-n8", isN8: true }]);
  const n8 = client.openN8("fake-n8");
  expect(n8.connect()).toBe(0);
  expect(n8.edio.sysInfo().length).toBe(64);
  expect(n8.edio.vdc().length).toBe(8);
  n8.close();
});

test("n8-load reads the card: info, free space, mkdir, a file put, listed, got back and removed", () => {
  expect(n8Load("--info").some((l) => l.startsWith("serial"))).toBe(true);
  expect(n8Load("--df")).toEqual(["SD free space: 0 (this N8 firmware doesn't report free space via CMD_F_AVB)"]);
  expect(n8Load("--mkdir", "songs")).toEqual(["made directory: songs"]);
  expect(n8Load("--mkdir", "songs")).toEqual(["made directory: songs"]); // an existing directory is done

  const data = new Uint8Array(3000).map((_, i) => (i * 13) & 0xff);
  expect(s.backend.writeFile(local("put.bin"), data)).toBe(true);
  expect(n8Load("--put-file", local("put.bin"), "songs/a.bin")).toEqual([
    `wrote 3000 bytes of ${local("put.bin")} -> songs/a.bin`,
  ]);
  expect(n8Load("--ls", "songs")).toEqual(["songs (1 entry):", "      3000  a.bin"]);
  expect(n8Load("--get-file", "songs/a.bin", local("got.bin"))).toEqual([
    `read 3000 bytes of songs/a.bin -> ${local("got.bin")}`,
  ]);
  expect(Array.from(s.backend.readFile(local("got.bin")) ?? [])).toEqual(Array.from(data));
  expect(n8Load("--rm", "songs/a.bin")).toEqual(["deleted: songs/a.bin"]);
  expect(n8Load("--ls", "songs")).toEqual(["songs (0 entries):"]);
  // What the CLI prints for a file the card does not hold, in the TS client's words.
  expect(() => n8Load("--rm", "songs/a.bin")).toThrow("Edio: operation error 0x04");
  expect(() => n8Load("--get-file", "songs/none.bin", local("none.bin"))).toThrow(
    "Edio: file not found on SD: songs/none.bin",
  );
  s.backend.deleteFile(local("put.bin"));
  s.backend.deleteFile(local("got.bin"));
});

test("n8-load boots a ROM with its battery through the menu, and the running cart answers as a game", () => {
  const rom = new Uint8Array(16 + 32768);
  rom.set([0x4e, 0x45, 0x53, 0x1a, 2, 0, 0x10, 0]); // iNES, mapper 1
  const srm = new Uint8Array(8192).fill(0x5a);
  expect(s.backend.writeFile(local("game.nes"), rom)).toBe(true);
  expect(s.backend.writeFile(local("game.srm"), srm)).toBe(true);
  expect(n8Load(local("game.nes"), "--srm", local("game.srm"))).toEqual([
    `booted 'usb-games/rp-n8-session-test-game.nes' (map index 69) with save ${local("game.srm")}`,
  ]);

  const n8 = createN8Client(makeCall()).openN8("fake-n8");
  try {
    n8.connect();
    // On the card where the menu put them, and the cart's config block names the game's mapper.
    expect(n8.readFile("usb-games/rp-n8-session-test-game.nes").length).toBe(rom.length);
    expect(Array.from(n8.readFile("EDN8/gamedata/rp-n8-session-test-game.nes/bram.srm").subarray(0, 4))).toEqual([
      0x5a, 0x5a, 0x5a, 0x5a,
    ]);
    expect(n8.mapConfig().mapIdx).toBe(69);
    expect(decodeSniffer(n8.edio.memRD(ADDR_SSR, SNIFFER_REGION_SIZE)).magicOk).toBe(false);
    // A running game has no menu: '*t' goes unanswered, which the CLI meets as an N8TimeoutError.
    let thrown: unknown;
    try {
      n8.menu.test();
    } catch (e) {
      thrown = e;
    }
    expect(thrown instanceof N8TimeoutError).toBe(true);
    expect((thrown as Error).message).toBe("Edio: serial read timeout (no N8 response)");
    // The cart's battery written straight and read back, as --sram-only does on a running game.
    expect(n8.writeSramDirect(new Uint8Array([1, 2, 3]))).toBe(3);
    expect(Array.from(n8.dumpSram().subarray(0, 3))).toEqual([1, 2, 3]);
  } finally {
    n8.close();
  }
  s.backend.deleteFile(local("game.nes"));
  s.backend.deleteFile(local("game.srm"));
});

test("a session does not open the port the streaming link holds, in the serial client's words", () => {
  const client = createN8Client(makeCall());
  const link = client.link;
  link.connect(true); // auto-picks fake-n8
  try {
    expect(link.linkConfig().connected).toBe(true);
    expect(() => client.openN8("fake-n8")).toThrow("cannot open serial port: fake-n8");
  } finally {
    link.connect(false);
  }
});
