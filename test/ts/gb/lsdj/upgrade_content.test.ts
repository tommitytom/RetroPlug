// Source B: content coverage for the newest sav formats (fmt17..22).
//
// liblsdj's content fixtures stop at fmt16, so booting a fmt16 content sav
// (lsdj888.sav) in each newer LSDj ROM lets LSDj upgrade the working song in
// place in SRAM — giving real fmt17..22 content with no hand-authoring. We then
// byte-identity round-trip each upgraded sav (decode -> re-encode from the model
// with the sav as template) to catch format-specific content codec bugs, exactly
// as the C++ content test does for fmt3..16.
//
// The first assertion (format byte at 0x7FFF advanced past 16) is the linchpin:
// it proves LSDj's upgrade persists to SRAM (the song lives in 32KB cart SRAM,
// not the 8KB GB work-RAM, so the upgrade is written in place there).
import { test, expect, emu, Mem } from "harness";

const SRC = "old/thirdparty/liblsdj/resources/sav/lsdj888.sav"; // fmt16 content
const CART_RAM = 0x2040; // instrumentAllocTable (64 bytes)
const FMT_OFF = 0x7fff;

// One newer-format ROM per target version (the empty-sav representatives).
const ROMS = [
  { fmt: 17, rom: "../resources/roms/lsdj/lsdj8_9_6-develop.gb" },
  { fmt: 18, rom: "../resources/roms/lsdj/lsdj9_0_1-develop.gb" },
  { fmt: 19, rom: "../resources/roms/lsdj/lsdj9_1_4-develop.gb" },
  { fmt: 20, rom: "../resources/roms/lsdj/lsdj9_1_A-develop.gb" },
  { fmt: 21, rom: "../resources/roms/lsdj/lsdj9_2_0-develop.gb" },
  { fmt: 22, rom: "../resources/roms/lsdj/lsdj9_4_2.gb" },
];

test("LSDj upgrades fmt16 content to fmt17..22 and the codec round-trips it", () => {
  const src = emu.readFile(SRC).buffer; // shared; loadRom copies the SRAM
  // Boot all targets from the same fmt16 song, then advance once — each ROM
  // upgrades its own copy in parallel.
  const systems = ROMS.map((r) => ({ ...r, sys: emu.loadRom(r.rom, src) }));
  emu.runMs(8000);

  for (const s of systems) {
    const up = emu.readMemory(s.sys, Mem.Sram);
    const fmt = up[FMT_OFF];
    const allocated = up.slice(CART_RAM, CART_RAM + 64).reduce((n, b) => n + (b ? 1 : 0), 0);
    const diff = emu.savRoundtripDiff(up.buffer);
    console.log(
      `[upgrade] ${s.rom.split("/").pop()}: fmt=${fmt} allocInstr=${allocated} roundtripDiff=${diff}` +
        (diff >= 0 ? ` (0x${diff.toString(16)})` : ""),
    );
    // Linchpin: the working song was upgraded past the fmt16 source.
    expect(fmt).toBeGreaterThan(16);
    // Content survived the upgrade (lsdj888 has allocated instruments).
    expect(allocated).toBeGreaterThan(0);
    // The codec reproduces this format's content byte-for-byte.
    expect(diff).toBe(-1);
  }
});
