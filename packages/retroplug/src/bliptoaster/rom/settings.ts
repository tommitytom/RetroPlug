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
const F_MODE1 = 9;
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
  /** Start in Mode 1: the screen stays dark until START is pressed. */
  mode1: boolean;
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
  mode1: false,
  velCurve: false,
  theme: 0,
  font: 0,
};

/** A partial edit: only the named fields are written, the rest of the block is left as the ROM baked it. */
export type BlipToasterSettingsPatch = Partial<BlipToasterSettings>;

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
    mode1: rom[at + F_MODE1] !== 0,
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
  if (patch.mode1 !== undefined) rom[at + F_MODE1] = patch.mode1 ? 1 : 0;
  if (patch.velCurve !== undefined) rom[at + F_VELCURVE] = patch.velCurve ? 1 : 0;
  if (patch.theme !== undefined) rom[at + F_THEME] = clamp(patch.theme, SETTINGS_THEME_COUNT);
  if (patch.font !== undefined) rom[at + F_FONT] = clamp(patch.font, SETTINGS_FONT_COUNT);
}
