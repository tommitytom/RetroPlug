// Launchpad message encoding - colours, LED lighting, and the mode/layout control messages.
//
// Protocol reference: the Launchpad Pro [MK3] Programmer's Reference Manual. Every builder here is
// checked byte-for-byte against a worked example from that manual in test/launchpad/protocol.test.ts,
// so the manual's own hex is the oracle rather than our reading of it.
//
// Pure: no I/O, no device, no RetroPlug types. A builder returns a raw MIDI message as number[]; who
// puts it on a wire is somebody else's problem.

import { controlKind, type LaunchpadProfile } from "./profile";

/** Every SysEx from or to the device starts `F0 00 20 29 02 <model>`. */
const SYSEX_START = 0xf0;
const SYSEX_END = 0xf7;
const SYSEX_HEADER = [0x00, 0x20, 0x29, 0x02] as const;

// Command bytes, from the manual's SysEx command summary.
const CMD_SELECT_LAYOUT = 0x00;
const CMD_LED_LIGHTING = 0x03;
const CMD_PROGRAMMER_TOGGLE = 0x0e;
const CMD_DAW_STANDALONE = 0x10;

/** Lighting behaviour is selected by MIDI CHANNEL on the short form: channel 1/2/3 = static/flash/pulse
 *  (so 0x90/0x91/0x92 for notes, 0xB0/0xB1/0xB2 for CCs). */
const NOTE_STATUS = 0x90;
const CC_STATUS = 0xb0;
const CHANNEL_STATIC = 0;
const CHANNEL_FLASH = 1;
const CHANNEL_PULSE = 2;

/** Lighting types for the bulk SysEx form. */
const TYPE_STATIC = 0x00;
const TYPE_FLASH = 0x01;
const TYPE_PULSE = 0x02;
const TYPE_RGB = 0x03;

/** The seven palette entries the manual names in prose. The full 128-entry table exists ONLY as an
 *  image in the PDF, so its RGB values are not extractable and are deliberately not invented here -
 *  a palette colour is an opaque 0..127 index, and anything needing an exact colour uses rgb(). */
export const Palette = {
  off: 0,
  red: 5,
  yellow: 13,
  green: 19,
  greenDim: 21,
  greenBright: 23,
  turquoise: 37,
  blue: 45,
} as const;

export type Colour =
  | { kind: "palette"; index: number }
  | { kind: "rgb"; r: number; g: number; b: number };

/** One LED's desired appearance. `alt` on a flash is the manual's "colour A" - the colour it alternates
 *  WITH, at 50% duty synced to MIDI beat clock. Pulse breathes one colour, also clock-synced. */
export type Led =
  | { mode: "off" }
  | { mode: "static"; colour: Colour }
  | { mode: "flash"; colour: Colour; alt: Colour }
  | { mode: "pulse"; colour: Colour };

export const LED_OFF: Led = { mode: "off" };

/** A palette colour by index (clamped to the legal 7-bit range). */
export function palette(index: number): Colour {
  return { kind: "palette", index: clamp7(index) };
}

/** A true-colour LED. Components are 7-bit (0..127 = min..max), per the manual's RGB lighting type. */
export function rgb(r: number, g: number, b: number): Colour {
  return { kind: "rgb", r: clamp7(r), g: clamp7(g), b: clamp7(b) };
}

function clamp7(v: number): number {
  if (!Number.isFinite(v)) return 0;
  return Math.max(0, Math.min(127, Math.round(v)));
}

/** True when this LED can only be expressed by the bulk SysEx form. RGB has no short-message encoding:
 *  a note's velocity is a palette index, so an RGB colour has nowhere to go. */
export function needsSysex(led: Led): boolean {
  if (led.mode === "off") return false;
  if (led.colour.kind === "rgb") return true;
  return led.mode === "flash" && led.alt.kind === "rgb";
}

/** True for an LED the device cannot render as asked. Only static supports RGB (lighting type 3 IS a
 *  static RGB colour); asking for an RGB flash or pulse would silently encode as something else, so
 *  callers get told instead. */
export function isRenderable(led: Led): boolean {
  if (led.mode === "flash") return led.colour.kind === "palette" && led.alt.kind === "palette";
  if (led.mode === "pulse") return led.colour.kind === "palette";
  return true;
}

// --- short form: one 3-byte message per LED ------------------------------------------------------

/** Light one control with a 3-byte Note/CC message. The status byte carries BOTH the addressing mode
 *  (note for a grid pad, CC for an edge button) and the lighting behaviour (the channel nibble).
 *  Returns null for an LED needing the SysEx form (RGB), so a caller cannot silently emit the wrong
 *  colour - use ledLightingSysex for those. */
export function lightMessage(index: number, led: Led): number[] | null {
  if (needsSysex(led) || !isRenderable(led)) return null;
  const status = controlKind(index) === "note" ? NOTE_STATUS : CC_STATUS;
  if (led.mode === "off") return [status | CHANNEL_STATIC, index, 0];
  const channel = led.mode === "flash" ? CHANNEL_FLASH : led.mode === "pulse" ? CHANNEL_PULSE : CHANNEL_STATIC;
  const colour = led.colour.kind === "palette" ? led.colour.index : 0;
  return [status | channel, index, colour];
}

// --- bulk form: one SysEx for up to `maxColourSpecs` LEDs -----------------------------------------

/** One LED's colour spec inside the bulk message: type, index, then 1-3 bytes of data. */
export function colourSpec(index: number, led: Led): number[] {
  switch (led.mode) {
    case "off":
      return [TYPE_STATIC, index, 0];
    case "static":
      return led.colour.kind === "rgb"
        ? [TYPE_RGB, index, led.colour.r, led.colour.g, led.colour.b]
        : [TYPE_STATIC, index, led.colour.index];
    case "flash":
      // "Lighting data is 2 bytes specifying Colour B and Colour A" - B (the flashed-to colour) first.
      return [TYPE_FLASH, index, colourIndexOf(led.colour), colourIndexOf(led.alt)];
    case "pulse":
      return [TYPE_PULSE, index, colourIndexOf(led.colour)];
  }
}

function colourIndexOf(colour: Colour): number {
  return colour.kind === "palette" ? colour.index : 0; // RGB is unrepresentable here; isRenderable guards it
}

/** Wrap raw payload bytes in the model's SysEx frame. */
function sysex(profile: LaunchpadProfile, command: number, payload: readonly number[]): number[] {
  return [SYSEX_START, ...SYSEX_HEADER, profile.sysexId, command, ...payload, SYSEX_END];
}

/** Bulk LED lighting: one message setting many LEDs at once, and the only way to send RGB. Splits into
 *  several messages when there are more entries than the model allows in one. */
export function ledLightingSysex(profile: LaunchpadProfile, entries: readonly { index: number; led: Led }[]): number[][] {
  const out: number[][] = [];
  for (let i = 0; i < entries.length; i += profile.maxColourSpecs) {
    const chunk = entries.slice(i, i + profile.maxColourSpecs);
    const payload: number[] = [];
    for (const e of chunk) payload.push(...colourSpec(e.index, e.led));
    out.push(sysex(profile, CMD_LED_LIGHTING, payload));
  }
  return out;
}

// --- mode + identification ------------------------------------------------------------------------

/** Enter Programmer mode, where every pad and button sends and accepts plain MIDI.
 *
 *  Two things the manual is emphatic about, and a host must honour both: the device ALWAYS boots into
 *  Live mode (so this has to be sent on every connect), and while Programmer mode is selected this way
 *  its Settings menu is locked out - so a host that quits without sending exitToLiveMode leaves the
 *  user's hardware in a state they cannot escape from the front panel. */
export function enterProgrammerMode(profile: LaunchpadProfile): number[] {
  return sysex(profile, CMD_PROGRAMMER_TOGGLE, [1]);
}

/** Return the device to Live mode. Send this on disconnect, and on shutdown. */
export function exitToLiveMode(profile: LaunchpadProfile): number[] {
  return sysex(profile, CMD_PROGRAMMER_TOGGLE, [0]);
}

/** Select a layout explicitly. Programmer mode is `profile.programmerLayout`; `page` is 0 for every
 *  view except custom modes / sequencer / faders / settings. */
export function selectLayout(profile: LaunchpadProfile, layout: number, page = 0): number[] {
  return sysex(profile, CMD_SELECT_LAYOUT, [layout, page, 0]);
}

/** Leave DAW mode (Session + DAW faders) for Standalone. We never ENABLE DAW mode - Programmer mode is
 *  the layer this module drives - but restoring Standalone on exit is what the manual asks of software
 *  that touched it, and costs nothing. */
export function setStandaloneMode(profile: LaunchpadProfile): number[] {
  return sysex(profile, CMD_DAW_STANDALONE, [0]);
}

/** Universal Device Inquiry - the model-independent way to find out what is on the other end. */
export function deviceInquiry(): number[] {
  return [SYSEX_START, 0x7e, 0x7f, 0x06, 0x01, SYSEX_END];
}

/** Parse a Device Inquiry REPLY into its Novation family bytes + app version, or null when the message
 *  is not one (or is from another manufacturer). Lets a host confirm it found a Launchpad rather than
 *  trusting a port name. */
export function parseInquiryReply(data: readonly number[]): { family: number[]; version: number[] } | null {
  //  0  1   2    3  4   5  6  7    8  9   10 11  12..15      16
  // F0 7E <dev> 06 02  00 20 29  <family x2> 00 00 <version x4> F7
  if (data.length < 17 || data[0] !== SYSEX_START || data[1] !== 0x7e || data[3] !== 0x06 || data[4] !== 0x02) return null;
  if (data[5] !== 0x00 || data[6] !== 0x20 || data[7] !== 0x29) return null; // not Novation
  return { family: [data[8], data[9]], version: data.slice(12, 16) };
}
