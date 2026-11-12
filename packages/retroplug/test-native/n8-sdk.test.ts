// The SDK's N8 end to end (connectN8, packages/retroplug/cli/sdk.ts, over @retroplug/n8/device): this host
// (retroplug-host, packages/native/src/main.cpp) starts the real N8 provider on FakeN8's port, so a session script's
// device runs through the facet and the one Edio exactly as on the CLI: the port it finds, the card's files, the
// sniffer region, a FIFO write, a ROM booted at the menu, and a closed device refusing a call.
import { test, expect } from "@retroplug/testing";
import { connectN8 } from "../cli/sdk";

test("connectN8 finds the fake cart, and the card's calls make, write, list, read and delete", () => {
  const n8 = connectN8();
  try {
    expect(n8.port).toBe("fake-n8");
    n8.makeDir("/sdk");
    const data = new Uint8Array(1500).map((_, i) => (i * 7) & 0xff);
    n8.writeFile("/sdk/a.bin", data);
    expect(n8.listDir("/sdk")).toEqual([{ name: "a.bin", size: 1500, isDir: false }]);
    expect(n8.listDir("/").some((e) => e.name === "sdk" && e.isDir)).toBe(true);
    expect(Array.from(n8.readFile("/sdk/a.bin"))).toEqual(Array.from(data));
    n8.deleteFile("/sdk/a.bin");
    expect(n8.listDir("/sdk")).toEqual([]);
    // A file the card does not hold, in the words a consumer's catch matches.
    expect(() => n8.deleteFile("/sdk/a.bin")).toThrow("Edio: operation error 0x04");
  } finally {
    n8.close();
  }
});

test("sniffRaw reads the sniffer region, writeFifo reaches the cart, and load boots at the menu", () => {
  const n8 = connectN8({ serial: "fake-n8", timeoutMs: 1000 });
  try {
    expect(n8.sniffRaw().length).toBe(512);
    n8.writeFifo(new Uint8Array([0x90, 0x3c, 0x7f])); // not a menu command: the menu skips it
    const rom = new Uint8Array(16 + 16384);
    rom.set([0x4e, 0x45, 0x53, 0x1a, 1, 0, 0, 0]); // iNES, mapper 0
    expect(n8.load({ romBytes: rom, romName: "sdk-boot.bin" })).toEqual({
      bootPath: "usb-games/sdk-boot.bin",
      mapIndex: 69,
    });
    expect(n8.readFile("usb-games/sdk-boot.bin").length).toBe(rom.length);
  } finally {
    n8.close();
  }
});

test("a closed device refuses every call in its own words", () => {
  const n8 = connectN8();
  n8.close();
  expect(() => n8.listDir("/")).toThrow("N8Device.listDir: the device is closed");
  expect(() => n8.writeFifo(new Uint8Array(0))).toThrow("N8Device.writeFifo: the device is closed");
});
