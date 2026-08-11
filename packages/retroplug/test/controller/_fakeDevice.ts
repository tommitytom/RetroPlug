// A fake Launchpad: something that receives what the host writes and ends up in a STATE, rather than a
// list of bytes to compare against.
//
// This is what makes "tested with no hardware" mean anything. A test that asserts on emitted hex only
// proves we still emit the hex we used to; this decodes the host's own messages back into per-LED state -
// short messages and bulk SysEx alike - so a test says "pad (2,3) is bright green" and fails when the
// DEVICE would end up wrong. It also round-trips src/launchpad's encoder against itself: if the encoder
// and this decoder ever disagree about the wire format, one of them is wrong and the tests say so.
//
// It models the two device behaviours that a host has to get right: entering Programmer mode BLANKS the
// surface (which is why ControllerSession.connect invalidates), and pads report note-off as Note On with
// velocity 0.

import { LED_OFF, PRO_MK3, padAt, padIndex, type LaunchpadProfile, type Led } from "../../src/launchpad";

const SYSEX_START = 0xf0;
const SYSEX_END = 0xf7;
const CMD_LED_LIGHTING = 0x03;
const CMD_PROGRAMMER_TOGGLE = 0x0e;

const staticLed = (index: number): Led =>
  index === 0 ? LED_OFF : { mode: "static", colour: { kind: "palette", index } };
const paletteColour = (index: number) => ({ kind: "palette" as const, index });

export class FakeLaunchpad {
  readonly profile: LaunchpadProfile;
  /** Every message written, in order - for the rare test that cares about message COUNT or form. */
  readonly writes: number[][] = [];
  /** False until the host says otherwise, because a real device always boots into Live mode. */
  programmerMode = false;

  private readonly leds = new Map<number, Led>();

  constructor(profile: LaunchpadProfile = PRO_MK3) {
    this.profile = profile;
  }

  /** Feed the device what a host just wrote. */
  write(messages: readonly (readonly number[])[]): void {
    for (const m of messages) {
      this.writes.push([...m]);
      if (m[0] === SYSEX_START) this.sysex(m);
      else this.shortMessage(m);
    }
  }

  /** The LED at a device index. */
  led(index: number): Led {
    return this.leds.get(index) ?? LED_OFF;
  }

  /** The LED at a grid coordinate (top-left origin, matching the rest of the API). */
  pad(x: number, y: number): Led {
    return this.led(padIndex({ x, y }));
  }

  /** The LED on a named edge button. */
  button(name: string): Led {
    const cc = this.profile.buttons[name];
    return cc === undefined ? LED_OFF : this.led(cc);
  }

  /** Every lit grid pad, as "x,y" -> Led. Lets a test assert the WHOLE picture rather than sampling it,
   *  which is how an unexpectedly-lit pad gets caught. */
  litPads(): Record<string, Led> {
    const out: Record<string, Led> = {};
    for (const [index, led] of this.leds) {
      const p = padAt(index);
      if (p && led.mode !== "off") out[`${p.x},${p.y}`] = led;
    }
    return out;
  }

  /** How many messages have been written since the last `clearWrites`. */
  clearWrites(): void {
    this.writes.length = 0;
  }

  // --- what the device SENDS -----------------------------------------------------------------------

  /** A pad press, as the raw MIDI the device would emit. */
  press(x: number, y: number, velocity = 100): number[] {
    return [0x90, padIndex({ x, y }), velocity];
  }

  /** A pad release. Note On with velocity 0 - what a Launchpad actually sends, not a real Note Off. */
  release(x: number, y: number): number[] {
    return [0x90, padIndex({ x, y }), 0];
  }

  pressButton(name: string): number[] {
    return [0xb0, this.profile.buttons[name] ?? 0, 127];
  }

  releaseButton(name: string): number[] {
    return [0xb0, this.profile.buttons[name] ?? 0, 0];
  }

  // --- decoding ------------------------------------------------------------------------------------

  private shortMessage(m: readonly number[]): void {
    if (m.length < 3) return;
    const status = m[0] & 0xf0;
    if (status !== 0x90 && status !== 0xb0) return;
    const channel = m[0] & 0x0f; // 0/1/2 = static/flash/pulse, exactly as the lighting form encodes it
    const index = m[1];
    const colour = m[2];
    // The short form carries ONE colour byte, so a flash sent this way alternates with colour A = off -
    // which is exactly what the encoder assumes (the manual's `91 51 13` example). The alt colour is not
    // recoverable from these three bytes, and does not need to be.
    if (channel === 1) this.leds.set(index, { mode: "flash", colour: paletteColour(colour), alt: paletteColour(0) });
    else if (channel === 2) this.leds.set(index, { mode: "pulse", colour: paletteColour(colour) });
    else this.leds.set(index, staticLed(colour));
  }

  private sysex(m: readonly number[]): void {
    // F0 00 20 29 02 <model> <command> ... F7
    if (m.length < 8 || m[m.length - 1] !== SYSEX_END) return;
    if (m[1] !== 0x00 || m[2] !== 0x20 || m[3] !== 0x29 || m[4] !== 0x02) return;
    if (m[5] !== this.profile.sysexId) return;

    const command = m[6];
    if (command === CMD_PROGRAMMER_TOGGLE) {
      this.programmerMode = m[7] === 1;
      // A real device blanks its surface on the mode change, which is precisely why a host's diffing
      // baseline goes stale here and connect() has to invalidate.
      this.leds.clear();
      return;
    }
    if (command !== CMD_LED_LIGHTING) return;

    let i = 7;
    while (i < m.length - 1) {
      const type = m[i];
      const index = m[i + 1];
      if (type === 0x00) {
        this.leds.set(index, staticLed(m[i + 2]));
        i += 3;
      } else if (type === 0x01) {
        // "2 bytes specifying Colour B and Colour A" - B (the flashed-to colour) first.
        this.leds.set(index, { mode: "flash", colour: paletteColour(m[i + 2]), alt: paletteColour(m[i + 3]) });
        i += 4;
      } else if (type === 0x02) {
        this.leds.set(index, { mode: "pulse", colour: paletteColour(m[i + 2]) });
        i += 3;
      } else if (type === 0x03) {
        this.leds.set(index, { mode: "static", colour: { kind: "rgb", r: m[i + 2], g: m[i + 3], b: m[i + 4] } });
        i += 5;
      } else {
        return; // an unknown lighting type has an unknown length, so the rest cannot be parsed
      }
    }
  }
}
