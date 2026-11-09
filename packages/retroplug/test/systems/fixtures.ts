// Minimal valid ROM byte buffers for systems-store tests. The mock classifies via
// detectPlatform, so a fixture only needs the right magic at the right offset.
// (Not a *.test.ts, so the runner ignores it; imported by the store tests.)

import { resolveSmsggdjLayout } from "@retroplug/smsggdj/runtime/layout";

const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];

const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];

/** A buffer that classifies as a Game Boy ROM (Nintendo logo at 0x104). */
export function gbRom(): Uint8Array {
  const b = new Uint8Array(0x8000);
  b.set(GB_LOGO, 0x104);
  return b;
}

/** A GB ROM whose header declares MBC1+RAM+BATTERY (0x147) + 8KB SRAM (0x149), so a REAL
 *  SameBoy core allocates battery RAM and saveSramBytes() is non-empty. (The mock ignores
 *  the cartridge header, so this only matters for the native-host integration tests.) */
export function gbRomBattery(): Uint8Array {
  const b = gbRom();
  b[0x147] = 0x03; // MBC1 + RAM + BATTERY
  b[0x149] = 0x02; // 8 KB SRAM
  return b;
}

/** A GB battery ROM whose cartridge title (0x134) reads "LSDJ", so the ROM providers attach the
 *  lsdj-sync role — and thus its load-time sav-seed hook. The mock ignores the cartridge header, so
 *  the title at 0x134 is what the provider keys on. */
export function lsdjRom(title = "LSDJ"): Uint8Array {
  const b = gbRomBattery();
  for (let i = 0; i < title.length && i < 0x10; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

/** A buffer that classifies as a GBA ROM (Nintendo logo at 0x04). */
export function gbaRom(): Uint8Array {
  const b = new Uint8Array(0x8000);
  b.set(GBA_LOGO, 0x04);
  return b;
}

/** A buffer that classifies as an NES ROM (iNES magic at 0). */
export function nesRom(): Uint8Array {
  const b = new Uint8Array(0x4000);
  b.set([0x4e, 0x45, 0x53, 0x1a], 0);
  return b;
}

/** A minimal but VALID iNES ROM (mapper 1 / MMC1, battery flag) that a real Mesen core boots and for
 *  which it allocates battery-backed save RAM — so saveSramBytes() / readSram() are non-empty. 16-byte
 *  header + one 16 KB PRG bank (CHR-RAM); PRG holds a JMP-to-self at the reset vector so the booted CPU
 *  idles instead of running off into garbage. (The mock ignores the header; this only matters for the
 *  native-host integration tests.) */
export function nesRomBattery(): Uint8Array {
  const PRG = 0x4000; // one 16 KB bank
  const b = new Uint8Array(0x10 + PRG);
  b.set([0x4e, 0x45, 0x53, 0x1a], 0); // "NES\x1A"
  b[4] = 0x01; // 1 x 16 KB PRG
  b[5] = 0x00; // 0 x 8 KB CHR → CHR-RAM
  b[6] = 0x12; // flags6: mapper low nibble = 1 (MMC1) | bit1 battery
  b[7] = 0x00; // flags7: mapper high nibble = 0, iNES v1
  // A single 16 KB bank maps to $C000-$FFFF. Put `JMP $C000` at $C000 and point the reset vector at it.
  b[0x10 + 0x0000] = 0x4c; // JMP abs
  b[0x10 + 0x0001] = 0x00;
  b[0x10 + 0x0002] = 0xc0; // → $C000 (infinite loop)
  b[0x10 + 0x3ffc] = 0x00; // reset vector low  ($FFFC)
  b[0x10 + 0x3ffd] = 0xc0; // reset vector high ($FFFD) → $C000
  return b;
}

// The risa ROM fixtures are @retroplug/risa's (a synthetic risa image is that cart's own knowledge: its theme table,
// kit bank and version marker), re-exported here so this tree's store, menu and registry tests keep one import.
export { risaRom, risaRomFull, setRisaVersionMarker } from "@retroplug/risa/testFixtures";

// The BlipToaster ROM fixtures are @retroplug/bliptoaster's (a synthetic BlipToaster image is that cart's own
// knowledge: its SIG block, theme table stride and bank geometry), re-exported here so this tree's store, menu and
// registry tests keep one import.
export {
  blipToasterRom,
  blipToasterMultiKitRom,
  blipToasterThemeTable,
  blipToasterSettingsBlock,
} from "@retroplug/bliptoaster/testFixtures";

/** An smsggdj build's ROM as the providers and the version gate see it: a "TMR SEGA" header whose region
 *  nibble says SMS (4) or Game Gear (6), the SMSGGDJ marker at $3640 and a `V<version>` string at $367B -
 *  where the real v0.45 build puts them (the version is NOT adjacent to the marker; the UI string table sits
 *  between). A supported version (0.45 / 0.46 / 0.47) resolves a work-RAM layout, so the live song path is
 *  open. */
export function smsggdjRom(version = "0.45", regionNibble = 4): Uint8Array {
  const b = new Uint8Array(0x8200);
  b.set([0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41], 0x7ff0); // "TMR SEGA"
  b[0x7ff0 + 0xf] = regionNibble << 4; // 4 -> SMS, 5-7 -> Game Gear
  for (let i = 0; i < "SMSGGDJ".length; i++) b[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  const v = `V${version}`;
  for (let i = 0; i < v.length; i++) b[0x367b + i] = v.charCodeAt(i);
  return b;
}

/** smsggdj work RAM (the 8 KB region readRam serves) as a test wants the cart to hold it: the working-song
 *  `block` at the base, the cart's own `song_name` (space-padded, as `rle_name_default` pads it), its
 *  `song_edited` flag, and `ints_on` - the "boot finished" latch. `booted` defaults to TRUE, a cart that
 *  is up, because that is what nearly every test means by "a cart"; `booted: false` is the boot window,
 *  in which the block, the name and the flags are the boot sequence's and not yet the cart's. */
export function smsggdjRam(opts: { block?: Uint8Array; name?: string; edited?: boolean; booted?: boolean } = {}): Uint8Array {
  const layout = resolveSmsggdjLayout("0.45")!;
  const ram = new Uint8Array(0x2000);
  if (opts.block) ram.set(opts.block, layout.song);
  if (opts.name !== undefined) {
    for (let i = 0; i < layout.nameLen; i++) ram[layout.name + i] = i < opts.name.length ? opts.name.charCodeAt(i) : 0x20;
  }
  ram[layout.edited] = opts.edited ? 1 : 0;
  ram[layout.booted] = (opts.booted ?? true) ? 1 : 0;
  return ram;
}

/** A present-but-not-a-ROM buffer (classifies "unknown"). */
export function garbage(): Uint8Array {
  return new Uint8Array(0x8000); // all zero
}

/** The "sameboy" system role's config as the schema fills it, with `overrides` applied on top.
 *
 *  Tests that assert a whole role config go through here rather than spelling the object out, so an
 *  ADDITIVE schema change (a new knob with a default) doesn't have to be pasted into every assertion.
 *  A non-additive change should still break them — that's the point of pinning the shape at all. */
export function sameboyRoleConfig(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    model: "cgbC",
    highpass: "accurate",
    linkGroupId: 0,
    fastBoot: true,
    colorCorrection: "disabled",
    dmgPalette: "grey",
    lightTemperature: 0,
    ...overrides,
  };
}
