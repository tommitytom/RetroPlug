// The BlipToaster BAKED SETTINGS block — the pure codec for the cart's rig configuration.
//
// BlipToaster has no settings memory: there is no battery, and nothing survives in RAM across a RESET on
// purpose, so what you would set once for your rig is baked into the `.nes` itself. It lives in a 16-byte
// magic-tagged block in the always-mapped code bank (`g_settings`, the ROM repo's src/core/settings.c), found
// by scanning for its magic exactly like the theme table. RetroPlug patches it the same NON-DESTRUCTIVE way it
// patches assets: the block is spliced into the ROM image in memory at construct, and the file on disk is never
// rewritten (see ../../bliptoasterAssetsRole).
//
// Every field is a BOOT DEFAULT, not a lock — the matching MIDI CC keeps working over it and the next RESET
// comes back to the baked value.
//
// The reader below MIRRORS the ROM's own per-field rule, and the two must stay in step (src/midi/main.c reads
// the channel and kit through `& 0x0F`, the flags through `!= 0`, and clamps the theme and font against their
// table sizes). That is not pedantry: it is what makes a value RetroPlug shows equal the value the cart will
// actually boot with, including for a byte no tool has ever written.
//
// `ppu` is the field to be careful with. Its byte was "Mode 1 at boot" (1 = dark until START) until 2026-09-13
// and is now "PPU enabled" (1 = the screen draws) - same byte, opposite sense, and the format stayed 1 because
// bumping it would make every existing tool refuse the whole block. Two consequences: it is the ONLY field
// whose power-on default is not 0, and a ROM built before the flip reads its own byte the old way, so patching
// one with this build writes the inverse of what it will do. Nothing in the block distinguishes the two, which
// is exactly why the flag that writes it was renamed rather than kept meaning something new.

/** `\xA5\x5ASETT` — the block's magic, the same shape as the theme table's. */
export const SETTINGS_MAGIC = [0xa5, 0x5a, 0x53, 0x45, 0x54, 0x54];
/** The whole block, magic included. A new field takes a reserved byte; the size never changes. */
export const SETTINGS_BLOCK_SIZE = 16;
/** The only format this build reads or writes. A ROM stamped anything else is left alone. */
export const SETTINGS_FORMAT = 1;

// Field offsets from the start of the block (src/core/settings.h SET_F_*).
const F_VERSION = 6;
const F_BASE_CH = 7;
const F_KIT = 8;
const F_PPU = 9;
const F_VELCURVE = 10;
const F_THEME = 11;
const F_FONT = 12;

/** How many baked DMC kit slots the default-kit field can name (the ROM's ch5 CC 14 range). */
export const SETTINGS_KIT_COUNT = 16;
/** How many baked themes / CHR fonts the two screen fields can name (THEME_COUNT / FONT_COUNT in the ROM's
 *  src/core/sys.h). The theme table's REAL length is read from the ROM (BlipToasterRom.themeCount); these are
 *  the bounds the ROM's own clamp uses, so they are what decides whether a byte survives the round trip. */
export const SETTINGS_THEME_COUNT = 16;
export const SETTINGS_FONT_COUNT = 4;

/** The cart's baked rig configuration, decoded. Every field is already the EFFECTIVE value — what the ROM will
 *  boot with — so an out-of-range byte reads as that field's power-on default rather than as itself. */
export interface BlipToasterSettings {
  /** Base MIDI channel as an OFFSET: 0 = BASE01 (channel 1), 3 = BASE04. */
  baseChannel: number;
  /** Default DMC kit slot, 0..15 — which bank ch5 plays out of at boot. */
  kit: number;
  /** PPU enabled at boot: the screen draws. False is the old "Mode 1" boot - dark until START is pressed.
   *  The only field whose default is TRUE (see the polarity note at the top). */
  ppu: boolean;
  /** The log velocity curve on every channel (ignored by the VRC7 build). False = linear. */
  velCurve: boolean;
  /** Default colour theme, 0..15 (CC 16 switches it live). */
  theme: number;
  /** Default CHR font, 0..3 (CC 17 switches it live). */
  font: number;
}

/** The power-on defaults: what a cart boots with when its block was never patched. Also what every field falls
 *  back to when its byte is out of range — which is the case that matters, since a tool older than a field
 *  leaves the reserved 0xFF sitting there. */
export const DEFAULT_SETTINGS: BlipToasterSettings = {
  baseChannel: 0,
  kit: 0,
  ppu: true, // the one non-zero default: a clobbered or never-written byte must boot a VISIBLE screen
  velCurve: false,
  theme: 0,
  font: 0,
};

/** A partial edit: only the named fields are written, the rest of the block is left as the ROM baked it. */
export type BlipToasterSettingsPatch = Partial<BlipToasterSettings>;

/** The RESERVED filler. Every unused byte of the block holds it, and a new field takes a reserved byte, so this
 *  doubles as an exact capability probe: see fieldIsSupported. */
const RESERVED = 0xff;

// Which byte each optional-in-practice field lives at, for that probe. The four original fields (channel, kit,
// PPU, curve) shipped with the block and are not listed — every ROM carrying the block reads them.
const FIELD_OFFSET: Partial<Record<keyof BlipToasterSettings, number>> = {
  theme: F_THEME,
  font: F_FONT,
};

/** Does the ROM's own build READ this field, or does it predate it?
 *
 *  The block is fixed-size with reserved bytes, and a new field takes one, so a ROM built before a field has
 *  that field's byte still at 0xFF while one built after has it at 0 (its power-on default, from the ROM's own
 *  `g_settings`). No writer ever produces 0xFF — every writer here normalizes, and the ROM's power-on value is
 *  0 for every field this probe covers — so 0xFF at one means exactly "this image's code does not look at this
 *  byte". (`ppu` defaults to 1 and is not covered: it shipped with the block, so every ROM reads it.)
 *
 *  That matters to the UI, not just to tidiness: offering a Theme or Font pick on a cart that ignores it looks
 *  precisely like the feature being broken. Fields the block shipped with are always supported. */
export function fieldIsSupported(rom: Uint8Array, at: number, field: keyof BlipToasterSettings): boolean {
  const off = FIELD_OFFSET[field];
  return off === undefined || rom[at + off] !== RESERVED;
}

const clamp = (value: number, count: number): number => (value >= 0 && value < count ? value : 0);

/** The format byte of the block at `at`. Gate every other accessor on this equalling SETTINGS_FORMAT: a block
 *  stamped anything else has a layout this build does not know, and half-decoding it is worse than ignoring it. */
export function settingsFormatAt(rom: Uint8Array, at: number): number {
  return rom[at + F_VERSION];
}

/** Decode the block at `at` into its effective values. `rom` must hold the whole block from `at`. */
export function decodeSettings(rom: Uint8Array, at: number): BlipToasterSettings {
  return {
    baseChannel: rom[at + F_BASE_CH] & 0x0f, // masked by the ROM, not clamped: 0x1F boots as BASE16
    kit: rom[at + F_KIT] & 0x0f,
    ppu: rom[at + F_PPU] !== 0, // 0xFF from an older tool therefore reads as enabled, which is the safe way round
    velCurve: rom[at + F_VELCURVE] !== 0,
    theme: clamp(rom[at + F_THEME], SETTINGS_THEME_COUNT),
    font: clamp(rom[at + F_FONT], SETTINGS_FONT_COUNT),
  };
}

/** Splice `patch`'s named fields into the block at `at`. Values are normalized the way decodeSettings reads
 *  them, so a write then a read round-trips; the reserved bytes and the magic are untouched. */
export function encodeSettings(rom: Uint8Array, at: number, patch: BlipToasterSettingsPatch): void {
  if (patch.baseChannel !== undefined) rom[at + F_BASE_CH] = patch.baseChannel & 0x0f;
  if (patch.kit !== undefined) rom[at + F_KIT] = patch.kit & 0x0f;
  if (patch.ppu !== undefined) rom[at + F_PPU] = patch.ppu ? 1 : 0;
  if (patch.velCurve !== undefined) rom[at + F_VELCURVE] = patch.velCurve ? 1 : 0;
  if (patch.theme !== undefined) rom[at + F_THEME] = clamp(patch.theme, SETTINGS_THEME_COUNT);
  if (patch.font !== undefined) rom[at + F_FONT] = clamp(patch.font, SETTINGS_FONT_COUNT);
}
