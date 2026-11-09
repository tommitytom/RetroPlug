// The shell's half of BlipToaster's ROM detection (packages/bliptoaster/ts/test/romDetect.test.ts holds the byte-level
// half): the PROVIDER only marks the cart and writes no chip, so which build it is belongs to the role's `deriveConfig`,
// and the shell's rolesForRom is what asks it, with as deep a read as the role needs. The reader here serves whatever
// length is asked for, so the label case's read past the short header lands.
import { test, expect } from "@retroplug/testing";
import { buildAppRegistry } from "../../src/appHost";
import { rolesForRom } from "../../src/rolesForRom";
import { blipToasterRom, nesRom } from "../systems/fixtures";

/** The fixture ROM with its iNES mapper number rewritten (FME-7, 69, is the pair a label has to separate). */
function withMapper(rom: Uint8Array, mapper: number): Uint8Array {
  const b = rom.slice();
  b[6] = (b[6] & 0x0f) | ((mapper & 0x0f) << 4);
  b[7] = (b[7] & 0x0f) | (mapper & 0xf0);
  return b;
}

/** Write the NUL-terminated on-screen chip label where the cart's RODATA carries it. */
function withLabel(rom: Uint8Array, label: string, at = 0x2f00): Uint8Array {
  const b = rom.slice();
  for (let i = 0; i < label.length; i++) b[at + i] = label.charCodeAt(i);
  b[at + label.length] = 0;
  return b;
}

test("the ROM's roles carry the build on the `bliptoaster` role, which derives it", () => {
  // The PROVIDER only marks the cart (it writes no chip, so there is one writer); which build it is belongs to the role's
  // `deriveConfig`, and the shell's rolesForRom is what asks it, with as deep a read as it needs (the label case reads
  // past the short header, which is why the reader here serves whatever length is asked for).
  const reg = buildAppRegistry();
  const chipOf = (rom: Uint8Array): string | undefined => {
    const roles = rolesForRom(
      reg,
      { readFilePrefix: (_path, length) => rom.subarray(0, length) },
      { core: "mesen", platform: "nes", romPath: "/roms/synth.nes" },
    );
    return (roles.find((r) => r.kind === "bliptoaster")?.config as { chip?: string } | undefined)?.chip;
  };
  expect(chipOf(withLabel(withMapper(blipToasterRom(), 69), "S5B"))).toBe("s5b");
  expect(chipOf(withMapper(blipToasterRom(), 85))).toBe("vrc7");
  expect(chipOf(withMapper(blipToasterRom(), 24))).toBe("vrc6");
  expect(chipOf(blipToasterRom())).toBe("2a03");
  // a non-BlipToaster NES ROM gets no such role at all
  const roles = rolesForRom(
    reg,
    { readFilePrefix: (_path, length) => nesRom().subarray(0, length) },
    { core: "mesen", platform: "nes", romPath: "/roms/game.nes" },
  );
  expect(roles.some((r) => r.kind === "bliptoaster")).toBe(false);
});

// The marker is a wire contract with the ROM repo's rom/src/core/sig.s, and it has moved twice ("EVERMIDI"
// -> "evermidi-n8" -> "bliptoaster"). Pin the exact bytes here so a drift is a failing test rather than a
// ROM that silently loses its Kits/Fonts/Themes menu. The pre-rename spellings must NOT be detected.
