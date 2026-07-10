// The Arduinoboy MIDIOUT (MI.OUT) decoder for the `lsdj-sync` role's mode 7 — the TS port of the
// native ArduinoboyMaster role (packages/native/src/system/sameboy/roles/ArduinoboyMaster.{hpp,cpp}).
// Per greenfield's "native owns bytes, TS owns meaning": native captures LSDj's raw outgoing serial
// bytes and feeds them to the kernel (ctx.serialOut); this module turns them into host MIDI.
//
// Two layers, both pure + testable in isolation:
//
//   1. FLAG-GATED FRAMING (arduinoboyDecodeSerialOut). LSDj's MI.OUT wire frames each command as
//      `1 data-present flag bit + 7 payload bits`, MSB-first, so a captured 8-bit byte reads as
//      `0x80|payload` and the native 8-bit `captureSerialOutBit` mis-frames it as high-bit "garbage".
//      We reconstruct the raw bit stream (MSB-first per captured byte) and apply: read 1 flag; if it
//      is 1, read 7 payload bits → a 7-bit command byte; else it's an idle bit. A frame that straddles
//      a block boundary is buffered in `state` (bitAcc/bitCount) across calls.
//
//   2. BYTE PROTOCOL (arduinoboyDecodeByte). The 7-bit command byte drives the Arduinoboy protocol
//      (verbatim from the trash80/Arduinoboy firmware, Mode_LSDJ_Midiout.ino):
//        realtime  0x7D/0x7E/0x7F → 0xFA/0xFC/0xF8 (start/stop/clock)
//        command   0x70..0x7C     → m = byte-0x70, the NEXT value byte completes it:
//                    m < 4    → NoteOn  ch m      (value 0 → NoteOff)
//                    m < 8    → CC      ch m-4   (CC# = m, a documented simplification)
//                    m < 0xC  → PC      ch m-8
//        value     0x00..0x6F     → completes a pending command (ignored if none pending)
//      Bytes >= 0x80 are not protocol bytes and are dropped without disturbing pending state.

const REALTIME_START = 0x7d; // → 0xFA
const REALTIME_STOP = 0x7e; // → 0xFC
const REALTIME_CLOCK = 0x7f; // → 0xF8
const CMD_RANGE_FIRST = 0x70;
const CMD_RANGE_LAST = 0x7c; // inclusive — last "needs a value" command

const MIDI_NOTE_ON_BASE = 0x90;
const MIDI_NOTE_OFF_BASE = 0x80;
const MIDI_CC_BASE = 0xb0;
const MIDI_PC_BASE = 0xc0;

/** Emit one decoded MIDI message (raw status + data bytes). */
export type MidiEmit = (data: number[]) => void;

/** Cross-block decoder state (persisted in the role's `ctx.state`). Fields are created lazily by the
 *  decoders, so a fresh `{}` is a valid starting state. */
export interface ArduinoboyState {
  // Byte protocol: a 0x70..0x7C command awaiting its value byte. `pendingCmd` is m = byte-0x70; stored
  // separately from a sentinel so an out-of-band realtime byte during the wait can't confuse us.
  pendingValueExpected?: boolean;
  pendingCmd?: number;
  // Flag framing: partial bits carried between blocks. `bitAcc` holds `bitCount` pending bits, MSB-first
  // (the oldest/most-significant bit is at position bitCount-1).
  bitAcc?: number;
  bitCount?: number;
}

// Complete a pending command with its value byte. `m` is the 0..11 command id (byte-0x70); channels are
// 0-indexed (GB channel 0..3 → MIDI channel low nibble). Mirrors ArduinoboyMaster::emitCommandValue.
function emitCommandValue(m: number, v: number, emit: MidiEmit): void {
  if (m < 4) {
    const ch = m;
    // value 0 → NoteOff. The firmware sends a NoteOff for the channel's most-recent note; without that
    // running state we emit NoteOff on note 0 — an unambiguous "channel quiet" signal downstream.
    if (v === 0) emit([MIDI_NOTE_OFF_BASE | ch, 0, 0]);
    else emit([MIDI_NOTE_ON_BASE | ch, v & 0x7f, 0x7f]);
  } else if (m < 8) {
    const ch = m - 4;
    // Simplest of the firmware's CC-encoding modes: CC number = m, value = v (documented simplification).
    emit([MIDI_CC_BASE | ch, m, v & 0x7f]);
  } else if (m < 0x0c) {
    const ch = m - 8;
    emit([MIDI_PC_BASE | ch, v & 0x7f]);
  }
  // m >= 0x0C: undefined per the firmware; drop.
}

/** Feed one already-de-framed protocol byte (0x00..0x7F) through the Arduinoboy state machine, pushing
 *  decoded MIDI into `emit`. The TS twin of ArduinoboyMaster::feed. */
export function arduinoboyDecodeByte(byte: number, state: ArduinoboyState, emit: MidiEmit): void {
  // Not part of the documented MI.OUT protocol — drop defensively without disturbing pending state.
  if (byte >= 0x80) return;

  // Realtime commands are single-byte and orthogonal to the command/value pairing — they can fire even
  // while we're waiting on a value byte without disturbing the wait.
  switch (byte) {
    case REALTIME_CLOCK: emit([0xf8]); return;
    case REALTIME_START: emit([0xfa]); return;
    case REALTIME_STOP: emit([0xfc]); return;
    default: break;
  }

  if (byte >= CMD_RANGE_FIRST && byte <= CMD_RANGE_LAST) {
    state.pendingCmd = byte - CMD_RANGE_FIRST; // start a pending command/value pair
    state.pendingValueExpected = true;
    return;
  }

  // Value byte (0x00..0x6F). Only meaningful when a command is pending.
  if (state.pendingValueExpected) {
    emitCommandValue(state.pendingCmd ?? 0, byte, emit);
    state.pendingValueExpected = false;
    state.pendingCmd = 0;
  }
}

/** Reset all decoder state (call on mode-flip / system reset). */
export function arduinoboyReset(state: ArduinoboyState): void {
  state.pendingValueExpected = false;
  state.pendingCmd = 0;
  state.bitAcc = 0;
  state.bitCount = 0;
}

/** Decode a block's worth of RAW captured serial-out bytes: reconstruct the MSB-first bit stream,
 *  strip the flag-gated framing, and drive the byte protocol. Partial bits (a frame split across a
 *  block boundary) persist in `state` for the next call. */
export function arduinoboyDecodeSerialOut(bytes: number[], state: ArduinoboyState, emit: MidiEmit): void {
  let acc = state.bitAcc ?? 0;
  let count = state.bitCount ?? 0;

  for (let i = 0; i < bytes.length; i++) {
    // Append this captured byte's 8 bits MSB-first: the new bits become the least-significant end, and
    // the oldest buffered bit stays at position (count-1).
    acc = ((acc << 8) | (bytes[i] & 0xff)) >>> 0;
    count += 8;

    // Drain whole frames while enough bits remain. A frame is `flag (1) [+ payload (7) if flag]`.
    for (;;) {
      if (count < 1) break;
      const flag = (acc >>> (count - 1)) & 1; // the oldest (most-significant) buffered bit
      if (flag === 0) {
        count -= 1; // idle bit — consume and continue
        continue;
      }
      if (count < 8) break; // flag says a 7-bit payload follows, but it hasn't fully arrived yet
      const cmd = (acc >>> (count - 8)) & 0x7f; // the 7 payload bits after the flag
      count -= 8;
      arduinoboyDecodeByte(cmd, state, emit);
    }

    // Keep only the still-buffered low `count` bits so `acc` can't overflow a 32-bit shift over a long run.
    acc = count > 0 ? (acc & ((1 << count) - 1)) >>> 0 : 0;
  }

  state.bitAcc = acc;
  state.bitCount = count;
}
