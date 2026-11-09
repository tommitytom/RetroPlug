// A role's ROM-derived config (docs/modular-graph-phase-f4a-design.md section 4.5): BlipToaster's `chip` is a fact
// about the ROM FILE, so the store re-derives it at every build - the role says how much of the file it needs, and the
// read happens only for a role that asks for it. Today that role is `bliptoaster`: a tagged ROM names its build in the
// short SIG block, an untagged one only in the on-screen label inside the PRG region, and a ROM that is not BlipToaster
// at all is never read deeper than the short header.
//
// The behaviour held here is the one the provider's header read had: a fresh load of an untagged S5B build gets `s5b`
// (not the ambiguous mapper's fallback), and a cart that is not BlipToaster pays no deep read. What changed is WHERE it
// is decided (the role's hook, not a NES branch in the shell) and that a fresh build now derives through the same
// function a loaded project does.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "@retroplug/core/systemRoles";
import { registerCoreRoles } from "../_coreRoles";
import { registerFeatureRoles } from "../_featureRoles";
import { registerBlipToasterRole } from "@retroplug/bliptoaster/bliptoasterRole";
import { registerBlipToasterAssetsRole } from "@retroplug/bliptoaster/bliptoasterAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { blipToasterRom, nesRom } from "./fixtures";

/** The shell's short header: the most a provider is shown for a console whose header sits at the start of the file. */
const ROLE_HEADER_LEN = 0x150;
/** The deep prefix an untagged BlipToaster ROM's build label needs (its linker's RODATA, inside the PRG region). */
const CHIP_SCAN_LEN = 0x4010;

/** A backend that records the longest prefix each ROM path was read at: the proof a deep read did or did not happen. */
class CountingBackend extends MockBackend {
  readonly longest = new Map<string, number>();
  override readFilePrefix(path: string, length: number): Uint8Array | null {
    const key = this.canonicalize(path);
    this.longest.set(key, Math.max(this.longest.get(key) ?? 0, length));
    return super.readFilePrefix(path, length);
  }
  longestFor(path: string): number {
    return this.longest.get(this.canonicalize(path)) ?? 0;
  }
}

function newStore() {
  const be = new CountingBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg); // the mesen system role
  registerFeatureRoles(reg); // the feature roles with a protocol node
  registerBlipToasterRole(reg); // the marker whose `deriveConfig` this file is about
  registerBlipToasterAssetsRole(reg);
  registerRomProviders(reg); // BlipToaster's marker, LSDj's, mGB's
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

/** The fixture ROM with its iNES mapper number rewritten (FME-7, 69, is the pair the label has to separate). */
function withMapper(rom: Uint8Array, mapper: number): Uint8Array {
  const b = rom.slice();
  b[6] = (b[6] & 0x0f) | ((mapper & 0x0f) << 4);
  b[7] = (b[7] & 0x0f) | (mapper & 0xf0);
  return b;
}

/** Write the 4-byte SIG chip tag at +14 from the marker, as the cart's own sig.s bakes it. */
function withSigChip(rom: Uint8Array, tag: string, markerAt = 0x10): Uint8Array {
  const b = rom.slice();
  for (let i = 0; i < 4; i++) b[markerAt + 14 + i] = i < tag.length ? tag.charCodeAt(i) : 0;
  return b;
}

/** Write the NUL-terminated on-screen chip label where the cart's RODATA carries it (an untagged ROM's only source). */
function withLabel(rom: Uint8Array, label: string, at = 0x2f00): Uint8Array {
  const b = rom.slice();
  for (let i = 0; i < label.length; i++) b[at + i] = label.charCodeAt(i);
  b[at + label.length] = 0;
  return b;
}

/** The chip a built system's bliptoaster role carries. */
function chipOf(store: SystemsStore): unknown {
  return (store.view()[0].roles.find((r) => r.kind === "bliptoaster")?.config as { chip?: unknown } | undefined)?.chip;
}

test("a fresh load of an untagged S5B build derives s5b, from the deep read its role asks for", () => {
  // The fixture predates the SIG chip tag, and FME-7 (69) is BOTH the base build's mapper and the Sunsoft 5B's, so the
  // header alone cannot say: the label inside the PRG region is the only source, and the store reads that far.
  const { be, store } = newStore();
  const rom = withLabel(withMapper(blipToasterRom(), 69), "S5B");
  be.seed("/roms/bt-s5b.nes", rom);
  expect(store.addSystem("/roms/bt-s5b.nes")).toBeTruthy();
  expect(chipOf(store)).toBe("s5b");
  expect(be.longestFor("/roms/bt-s5b.nes") >= CHIP_SCAN_LEN).toBe(true);
});

test("a tagged build derives its tag, and pays no deep read", () => {
  const { be, store } = newStore();
  be.seed("/roms/bt-tagged.nes", withSigChip(withMapper(blipToasterRom(), 69), "S5B"));
  expect(store.addSystem("/roms/bt-tagged.nes")).toBeTruthy();
  expect(chipOf(store)).toBe("s5b");
  expect(be.longestFor("/roms/bt-tagged.nes")).toBe(ROLE_HEADER_LEN); // the SIG block is inside the short header
});

test("a cart that is not BlipToaster is never read deeper than the short header", () => {
  // No role asks for more, so no NES cart pays the 16 KB read: the provider's header is all anything reads.
  const { be, store } = newStore();
  be.seed("/roms/game.nes", nesRom());
  expect(store.addSystem("/roms/game.nes")).toBeTruthy();
  expect(store.view()[0].roles.some((r) => r.kind === "bliptoaster")).toBe(false);
  expect(be.longestFor("/roms/game.nes")).toBe(ROLE_HEADER_LEN);
});

test("a loaded project re-derives the chip from the ROM on disk, over the stored value", () => {
  // The loaded path runs the same derivation: a project stored while the chip was the ambiguous mapper's fallback (or
  // before the field existed) reads the build the ROM is today, in the same construct.
  const { be, store } = newStore();
  be.seed("/roms/bt-late.nes", withLabel(withMapper(blipToasterRom(), 69), "S5B"));
  const id = store.adopt({
    core: "mesen",
    romPath: "/roms/bt-late.nes",
    roles: [
      { kind: "mesen", config: {} },
      { kind: "bliptoaster", config: { chip: "2a03" } },
      { kind: "bliptoaster-assets", config: { overrides: [] } },
    ],
  });
  expect(id).toBeTruthy();
  expect(chipOf(store)).toBe("s5b");
});

test("a stored chip survives a ROM the backend cannot read", () => {
  // The old load path returned the stored roles unchanged when the read came back empty, and it must still: a
  // derivation run on zero bytes would write the fallback (2a03) over the chip the project stored. Only a Sega platform
  // classifies from the extension alone, so that is where a missing file reaches the roles with nothing to read.
  const { store } = newStore();
  let handed: readonly { kind: string; config: Record<string, unknown> }[] = [];
  (store as unknown as { boot: (spec: unknown, entry: { roles: typeof handed }) => null }).boot = (_spec, entry) => {
    handed = entry.roles;
    return null;
  };
  const id = store.adopt({
    core: "mesen",
    romPath: "/roms/bt-gone.sms",
    roles: [
      { kind: "mesen", config: {} },
      { kind: "bliptoaster", config: { chip: "vrc7" } },
      { kind: "bliptoaster-assets", config: { overrides: [] } },
    ],
  });
  expect(id === null).toBe(true); // the intercepted boot placed nothing; the roles it was handed are what this pins
  expect(handed.find((r) => r.kind === "bliptoaster")?.config.chip).toBe("vrc7");
});
