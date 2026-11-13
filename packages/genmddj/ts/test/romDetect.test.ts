// genmddj's identity and the provider that gives it its role: the product code AND a version string, and nothing else,
// because the sync's pins are player 2's buttons to every other game.
import { test, expect } from "@retroplug/testing";
import type { RomContext } from "@retroplug/core/systemRoles";
import { isGenmddjRom, identifyGenmddjVersion } from "../romDetect";
import { GENMDDJ_ROM_PROVIDER } from "../romProvider";
import { genmddjHeader } from "../fixtures";

// What the store hands a provider (core/systemRoles.ts's RomContext): the platform and core that were picked, the
// header prefix the descriptor asked for, and the embedded-ROM marker ("" for a file-backed ROM).
const ctx = (platform: string, core: string, header: Uint8Array): RomContext =>
  ({ platform, core, header, embeddedRom: "" });

test("genmddj is its product code and its version string", () => {
  expect(isGenmddjRom(genmddjHeader())).toBe(true);
  expect(identifyGenmddjVersion(genmddjHeader())).toBe("0.27");
  expect(identifyGenmddjVersion(genmddjHeader("V1.10"))).toBe("1.10");
});

test("the version string is found wherever the build puts it, not at one offset", () => {
  // The real ROM has it at $D1B1, past a 32 KiB scan and past the 0x150-byte short header; a build could move it, and
  // the scan is the whole reason the identity is not pinned to an offset. The $D1B1 case is what pins VERSION_SCAN_LEN
  // at 0x10000 (the real ROM's first 32 KiB carry no version string at all), and the $400 one is the build that moves
  // it somewhere shallower.
  expect(identifyGenmddjVersion(genmddjHeader("V0.27", "GM GENMDDJ-00", 0xd1b1))).toBe("0.27");
  expect(identifyGenmddjVersion(genmddjHeader("V0.27", "GM GENMDDJ-00", 0x400))).toBe("0.27");
});

test("either half alone is not genmddj", () => {
  expect(isGenmddjRom(genmddjHeader("V0.27", "GM OTHER  -00"))).toBe(false);
  const noVersion = genmddjHeader();
  noVersion.fill(0x00, 0xd1b1, 0xd1c0); // the version string's own bytes, where this fixture puts it
  expect(isGenmddjRom(noVersion)).toBe(false);
});

test("a short prefix is not genmddj rather than a throw", () => {
  expect(isGenmddjRom(new Uint8Array(0x100))).toBe(false);
  // The role provider's default prefix is 0x150, which ends before the product code at $180: an md ROM read at that
  // length is not genmddj, which is why the descriptor declares providerHeaderBytes 0x10000.
  expect(isGenmddjRom(genmddjHeader().slice(0, 0x150))).toBe(false);
});

test("the provider attaches genmddj-sync on md and nothing to anything else", () => {
  const rom = genmddjHeader();
  expect(GENMDDJ_ROM_PROVIDER(ctx("md", "blastem", rom))).toEqual([{ kind: "genmddj-sync", config: {} }]);
  expect(GENMDDJ_ROM_PROVIDER(ctx("sms", "mesen", rom))).toEqual([]);
  expect(GENMDDJ_ROM_PROVIDER(ctx("md", "blastem", genmddjHeader("V0.27", "GM OTHER  -00")))).toEqual([]);
});
