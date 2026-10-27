// The Arduinoboy MIDIOUT (MI.OUT) decoder for the `lsdj-sync` role's mode 7 — the TS port of the
// native ArduinoboyMaster role (packages/native/src/system/sameboy/roles/ArduinoboyMaster.{hpp,cpp}).
// Per the "native owns bytes, TS owns meaning" split: native captures LSDj's raw outgoing serial
// bytes, which reach the rp.lsdj.sync node on its `serialOut` input; this module turns them into host MIDI.
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

/** Cross-block decoder state (kept in the lsdj-sync node's scratch). Fields are created lazily by the
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

// --- Master Sync (Arduinoboy Mode 2, SYNC=LSDJ) --------------------------------------------------
//
// Here LSDj is the serial MASTER: playing, it self-clocks and streams one byte out its link port per
// MIDI-clock tick. captureSerialOutBit already assembles 8 serial-clock pulses into one byte, so — per
// the trash80/Arduinoboy Mode_LSDJ_MasterSync.ino firmware — each captured byte maps to exactly ONE
// MIDI clock (0xF8). This is the inverse of MidiSync (mode 1, where the host clocks LSDj): the DAW
// follows LSDj's tempo. No flag-framing (that's MI.OUT's slave protocol) — just byte → clock plus the
// transport bookends the firmware sends: a song-row NoteOn + 0xFA on the first byte of a run, 0xFC when
// the stream goes idle (LSDj stopped).

// LSDj's clock is the bytes it clocks out itself, which the core captures in its `listen` mode, never `drive`: a
// stopped LSDj master waits on an external clock for another master, and clocked, it turns slave and ignores START
// (docs/lsdj-master-sync-fix-design.md). So every byte is a clock, and a stopped LSDj goes quiet, which makes the
// firmware's stop a silence. A playing master clocks one byte per 24-PPQN tick, 62.5 ms apart at LSDj's slowest tempo
// (40 BPM), so a silence this long is a stop: about three times that gap. It is measured on the bytes' own frames, so
// the message sequence does not depend on how the host cuts its blocks. The 0xFC's time does: like every message this
// decoder sends, it goes out at frame 0, here of the first block that starts more than MASTER_SYNC_STOP_MS after the
// last byte, so it is quantised to a block start and up to a block late.
const MASTER_SYNC_STOP_MS = 200;

/** Cross-block Master Sync state (the lsdj-sync stage's `masterSync` sub-object, dropped whenever the mode is not
 *  master sync, so coming back starts clean). */
export interface MasterSyncState {
  started?: boolean; // a run is in progress (0xFA sent; 0xFC pending on the silence that ends it)
  channel?: number; // MIDI channel for the song-row NoteOn (default 0)
  lastAt?: number; // the run's newest byte: frames from the current block's start (negative: an earlier block)
  lastFrames?: number; // the last call's block length: where this call's bytes, from that block, sit
}

/** The block a Master Sync call runs in: its length and rate. */
export interface MasterSyncBlock {
  frames: number;
  sampleRate: number;
}

/** Decode the Master Sync serial-out bytes that reached this block into host MIDI. Call once per block with the
 *  bytes the lsdj-sync node read off its `serialOut` input and each one's frame, which are LAST block's bytes at
 *  their frames in it (the feedback wire's one block), and this block's length and rate. Each byte is a tempo clock, the first of a run
 *  opened with LSDj's song row; a run ends with one 0xFC once MASTER_SYNC_STOP_MS pass with no byte. */
export function arduinoboyMasterSyncBlock(
  bytes: number[],
  frames: number[],
  block: MasterSyncBlock,
  state: MasterSyncState,
  emit: MidiEmit,
): void {
  const ch = state.channel ?? 0;
  const from = state.lastFrames ?? block.frames; // the length of the block these bytes came from
  const stop = Math.round((block.sampleRate * MASTER_SYNC_STOP_MS) / 1000);
  for (let i = 0; i < bytes.length; i++) {
    const at = frames[i] - from;
    // A silence can end inside this call, with no block start between its 200 ms mark and the byte that ends it.
    // Checked only at a block start, the restart would carry on the old run (no 0xFC, no fresh NoteOn and 0xFA), so
    // the output would depend on the block split.
    if (state.started && at - (state.lastAt ?? 0) > stop) {
      emit([0xfc]);
      state.started = false;
    }
    if (!state.started) {
      // First tick of a run: the byte value is LSDj's song row → NoteOn, then transport start, then this
      // byte's own clock tick (the firmware sends NoteOn + 0xFA before the first 0xF8).
      state.started = true;
      emit([0x90 | ch, bytes[i] & 0x7f, 0x7f]);
      emit([0xfa]);
    }
    emit([0xf8]); // one MIDI clock per tempo byte
    state.lastAt = at;
  }
  // Every byte before this block's start is known: a run whose newest byte lies further back than the stop's
  // silence has ended.
  if (state.started && -(state.lastAt ?? 0) > stop) {
    emit([0xfc]);
    state.started = false;
  }
  if (state.started) state.lastAt = (state.lastAt ?? 0) - block.frames;
  state.lastFrames = block.frames;
}
