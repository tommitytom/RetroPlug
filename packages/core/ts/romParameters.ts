// A ROM's DAW automation parameters, as the cart that owns the ROM describes them (docs/modular-graph-phase-f4a-design.md
// section 4.5): one lane per control a build responds to, and the expansion of a build's CC table into those lanes. The
// shell's projectParameterMap (packages/retroplug/src/parameterMap.ts) projects the lanes onto the plugin's fixed slot
// pool; a cart's app module supplies the table, so the shell knows no cart's CC set and no cart's build names.
//
// The tables themselves are transcribed from each ROM's own documentation, not guessed (mGB's from the MIDI
// implementation map in trash80/mGB's README, BlipToaster's from the per-chip tables in its docs/chips/*.md).

/** One control a ROM exposes, expanded to a single automation lane. */
export interface RomParameter {
  /** 0-based MIDI channel the ROM listens on for this control, relative to the ROM's first voice. */
  channel: number;
  /** MIDI CC number, 0..127. */
  cc: number;
  /** Full name for the host's parameter list. */
  name: string;
  /** Abbreviated name for hosts that show a short parameter label. */
  shortName: string;
}

/** One CC a ROM responds to, and which of its 0-based channels accept it. */
export interface CcSpec {
  cc: number;
  /** Parameter name, prefixed with the voice name unless `shared`. */
  name: string;
  /** Abbreviated name, prefixed with the voice's short tag unless `shared`. */
  short: string;
  /** 0-based channels that respond to it. */
  channels: number[];
  /** The control is chip-global rather than per-voice (one register shared by several channels), so it
   *  gets ONE lane, addressed on `channels[0]`, with no voice prefix. */
  shared?: boolean;
}

/** A ROM build: what its voices are called, and every CC it responds to. */
export interface RomSpec {
  /** 0-based channel -> voice name. A gap (undefined) means that channel carries no voice. */
  voices: (readonly [full: string, short: string] | undefined)[];
  ccs: CcSpec[];
}

// Deliberately NOT exposed as lanes, on every ROM: the RPN bend-range handshake (CC101/100/6/38 is a
// three-message sequence, not a value) and the channel-mode messages (CC120 All Sound Off, CC121 Reset
// All Controllers, CC123 All Notes Off) - automating a panic message is actively harmful.

/** Expand a build's CC table into one lane per (CC, channel), in channel-then-CC order so a host's
 *  parameter list reads voice by voice. A `shared` CC contributes a single un-prefixed lane. */
export function expandRomSpec(spec: RomSpec): RomParameter[] {
  const out: RomParameter[] = [];

  for (let ch = 0; ch < spec.voices.length; ch++) {
    const voice = spec.voices[ch];
    if (!voice) continue;
    for (const c of spec.ccs) {
      if (!c.channels.includes(ch)) continue;
      if (c.shared) {
        if (c.channels[0] !== ch) continue; // one lane only, on its first channel
        out.push({ channel: ch, cc: c.cc, name: c.name, shortName: c.short });
      } else {
        out.push({
          channel: ch,
          cc: c.cc,
          name: `${voice[0]} ${c.name}`,
          shortName: `${voice[1]} ${c.short}`,
        });
      }
    }
  }

  return out;
}
