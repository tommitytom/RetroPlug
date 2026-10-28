// What the lsdj-sync modes put on LSDj's link port (host -> LSDj) and the button its
// auto-arm presses: shared by the role's schema (dspRoles.ts) and its protocol node (script/nodes/lsdjSync.ts). The keyboard
// mode's scancodes are lsdjKeyboardMap.ts's, and the decoders for what LSDj sends back are lsdjArduinoboy.ts's. A leaf:
// it imports nothing.

/** GameboyButton::Start, which autoStart taps on the transport's rise. */
export const LSDJ_START_BUTTON = 7;
/** A 24-PPQN MIDI clock tick (MidiSync, and the Arduinoboy mode while its play flag is set). */
export const LSDJ_CLOCK = 0xf8;
/** Transport start: the Arduinoboy mode's bookend. */
export const LSDJ_START = 0xfa;
/** Transport stop. */
export const LSDJ_STOP = 0xfc;
/** The MI.MAP NoteOff handshake sentinel. */
export const MIDIMAP_NOTEOFF = 0xfe;
/** The MI.MAP tick: Arduinoboy's Mode_LSDJ_Map setMapByte(0xFF) on a 0xF8. */
export const MIDIMAP_CLOCK = 0xff;

// MIDI status helpers. DPF hands the full status byte (channel in the low nibble) in data[0].
export const isNoteOn = (status: number): boolean => (status & 0xf0) === 0x90;
export const isNoteOff = (status: number): boolean => (status & 0xf0) === 0x80;
export const channelOf = (status: number): number => status & 0x0f;

// MidiMap row byte: ch0 NoteOn → note; ch1 → note + 128; other channels skipped (-1). Exported so the
// controller layer's launch ENCODER (src/controller/trackerTarget.ts) can be round-tripped against the
// decoder that actually ships, rather than restating the same convention twice and hoping they agree.
export const midiMapRow = (channel: number, note: number): number => (channel === 0 ? note : channel === 1 ? note + 128 : -1);
