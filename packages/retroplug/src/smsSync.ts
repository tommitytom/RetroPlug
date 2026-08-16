// The smsggdj host-sync transport (the Master System tracker, driven over controller port 2).
//
// NOT a byte protocol, unlike risa's FIFO stream (risaSync.ts) or LSDj's serial. What crosses the wire
// is a PORT LEVEL WORD: a 2-bit counter held on two input lines, which the ROM samples once per video
// frame and turns into a clock count. The `sms-sync` DSP role (dspRoles.ts) generates these from the
// DAW transport and delivers them over ctx.pushCoreBytes, which MesenSmsSystem routes to SmsSyncRole
// rather than to any MIDI or serial path. Pure + unit-tested (test/dsp/sms-sync.test.ts).
//
// BOTH MACHINES ARE SUPPORTED, and they differ only in which pins carry the counter: Master System
// uses controller port 2 ($DD, via SmsControlManager::SetExternalInput), Game Gear uses the EXT
// parallel port ($01, via SmsMemoryManager::SetGgExternalInput). See ggSyncLevels at the bottom.
//
// Protocol reference: smsggdj's GGSYNC.md and src/engine.asm:576-590 (`sync_read`) / :646-661
// (`sync_in_delta`). The encoding below is the same one the native external-input guard already drives
// (packages/native/test/audio/SmsAudio.test.cpp), so it is verified against a live core in tree.
//
// Three properties of the ROM side shape everything here:
//
//   The slave reads DELTAS, not absolutes. `sync_in_delta` computes `(current - last) & 3` once per
//   video frame, so the host's absolute counter value never matters - only that it advances by one per
//   clock. Up to 3 clocks between polls survive; a 4th aliases to 0 and is lost, which is what caps
//   the usable tempo (see SMS_SYNC_MAX_CLOCKS_PER_POLL).
//
//   Arming latches the live level. On the ROM's own Play in a slave mode it stores the current line
//   state into `sync_last`, sets `sync_wait`, and shows WAIT (engine.asm:383-388), so a stale level
//   cannot count as a clock and the first real one counts as exactly 1 whatever its size. The host
//   therefore never needs to synchronise its counter with the ROM's, and there is no arm packet.
//
//   There is NO LOCATE. The ROM only ever sees deltas, so the DAW drives tempo, start and stop but
//   NOT position - a seek does not relocate the song. That is smsggdj's design rather than a gap
//   here, but it is a real difference from risa-sync and lsdj-sync, both of which do relocate.

/** smsggdj's IN24 slave mode divides a 24-PPQN stream by 6, giving four rows per quarter note. */
export const SMS_SYNC_PPQN = 24;

// smsggdj's build marker, an ASCII string baked into the ROM ("SMSGGDJ" at $3640 in v0.45, with the
// version right after it at $367B). Sega carts carry no title field - the header is 16 bytes of magic,
// checksum, product code and region at the END of a bank - so a build marker is the only identity
// available, exactly as it is for risa on the NES.
const SMSGGDJ_MARKER = "SMSGGDJ";

// How far in to look. The marker sits well past any header, so the caller has to have read a deep
// prefix (SEGA_SNIFF_LEN); a short buffer simply fails to match rather than throwing.
const MARKER_SEARCH_LEN = 0x8000;

/** True when `rom` is an smsggdj build, by its baked-in ASCII marker. Used for both machines - the
 *  marker is in the shared source, so the `.gg` build carries it too.
 *
 *  This gate is not tidiness, it is correctness. On SMS the transport drives $DD bits 2/3/7, which are
 *  Player 2's TL, TR and TH lines - every SMS game reads that port. Attaching `sms-sync` to all of
 *  them the way `nes-n8-midi` attaches to every NES ROM would inject phantom Player-2 button presses
 *  into any cart whenever the DAW rolled. The NES case gets away with it because the N8 FIFO is a
 *  memory-mapped port that non-N8 ROMs ignore; a controller port has no such luxury. The GG EXT port
 *  is less contended, but a link-cable game polling PC4-PC6 would see the same phantom traffic, so it
 *  takes the same gate. */
export function isSmsggdjRom(rom: Uint8Array): boolean {
  const end = Math.min(rom.length, MARKER_SEARCH_LEN);
  const first = SMSGGDJ_MARKER.charCodeAt(0);
  outer: for (let i = 0; i + SMSGGDJ_MARKER.length <= end; i++) {
    if (rom[i] !== first) continue;
    for (let j = 1; j < SMSGGDJ_MARKER.length; j++) {
      if (rom[i + j] !== SMSGGDJ_MARKER.charCodeAt(j)) continue outer;
    }
    return true;
  }
  return false;
}

/** Idle level: every line released. Both counter bits read HIGH, so this is counter value 3. */
export const SMS_SYNC_IDLE_LEVELS = 0xff;

// Port 2 line bits within the $DD register. TL and TR are ANDed by the ROM for counter bit 0
// (`and $0C / cp $0C`), so TL is left released and TR alone carries the bit.
const SMS_TL_BIT = 0x04; // $DD bit 2 - P2 button 1; held HIGH so "TR AND TL" reduces to TR
const SMS_TR_BIT = 0x08; // $DD bit 3 - P2 button 2; counter bit 0
const SMS_TH_BIT = 0x80; // $DD bit 7 - P2 TH;       counter bit 1

/** The 2-bit counter is carried on two lines, so it wraps every four clocks. */
export const SMS_SYNC_COUNTER_MOD = 4;

/** The ROM polls once per video frame and recovers `(current - last) & 3`, so at most 3 clocks can be
 *  delivered between polls before the 4th aliases to zero and the song loses a beat. At 24 PPQN on a
 *  ~59.92 Hz NTSC frame this caps the usable tempo near 450 BPM (375 PAL). */
export const SMS_SYNC_MAX_CLOCKS_PER_POLL = 3;

/** The controller-port level word for 2-bit counter value `counter`.
 *
 *  Counter bits are active HIGH at the port: a line reading high is a 1, so the host PULLS A LINE LOW
 *  to signal a zero bit. (GGSYNC.md section 2.4 labels this "Active LOW", which describes the mask
 *  mechanic - clearing a mask bit pulls a line low - not the counter encoding. Inverting it would make
 *  the counter run backwards.) TL is always left high so the ROM's "TR AND TL" reduces to TR alone. */
export function smsSyncLevels(counter: number): number {
  const c = ((counter % SMS_SYNC_COUNTER_MOD) + SMS_SYNC_COUNTER_MOD) % SMS_SYNC_COUNTER_MOD;
  let levels = SMS_SYNC_IDLE_LEVELS & 0xff;
  levels |= SMS_TL_BIT; // never driven, but say so rather than relying on the idle word
  if ((c & 1) === 0) levels &= ~SMS_TR_BIT & 0xff;
  if ((c & 2) === 0) levels &= ~SMS_TH_BIT & 0xff;
  return levels;
}

// --- Game Gear: the same counter, different pins ---------------------------------------------
//
// The Game Gear has dedicated link I/O rather than a second controller port, so its build reads the
// EXT parallel port ($01, PC0-PC6) instead of $DD. Everything above still holds - the same 2-bit
// counter, the same once-per-frame `(current - last) & 3`, the same arm-latches-the-level, the same
// absence of a locate. ONLY THE BIT POSITIONS DIFFER, which is why this is one role with two
// encoders rather than two protocols.
//
// Reference: smsggdj's GGSYNC.md ("Chosen mechanism: parallel 2-bit counter") - counter bit 0 is
// PC4 AND PC5, counter bit 1 is PC6.

/** Idle level for the GG EXT port: PC0-PC6 all high (their pull-ups), so counter value 3 - the same
 *  idle the SMS word encodes, and bit-identical to what the ROM's own startup latch leaves behind. */
export const GG_SYNC_IDLE_LEVELS = 0x7f;

const GG_PC4_BIT = 0x10; // $01 bit 4 - EXT pin 6 / serial TX
const GG_PC5_BIT = 0x20; // $01 bit 5 - EXT pin 9 / serial RX
const GG_PC6_BIT = 0x40; // $01 bit 6 - EXT pin 7

/** The EXT-port level word for 2-bit counter value `counter`, the Game Gear twin of smsSyncLevels.
 *
 *  PC4 and PC5 are driven TOGETHER for counter bit 0, which is not redundancy for its own sake: the
 *  ROM computes that bit as `PC4 AND PC5` so that both a direct bridge connection (PC4 left open, its
 *  pull-up making the AND equal PC5) and a stock crossed Gear-to-Gear cable decode the same value.
 *  Driving only one of them would work on the emulator and then behave differently on hardware
 *  depending on the cable, so both are driven. */
export function ggSyncLevels(counter: number): number {
  const c = ((counter % SMS_SYNC_COUNTER_MOD) + SMS_SYNC_COUNTER_MOD) % SMS_SYNC_COUNTER_MOD;
  let levels = GG_SYNC_IDLE_LEVELS & 0xff;
  if ((c & 1) === 0) levels &= ~(GG_PC4_BIT | GG_PC5_BIT) & 0xff;
  if ((c & 2) === 0) levels &= ~GG_PC6_BIT & 0xff;
  return levels;
}

/** The two machines this role can clock. Serialized in the role config, so it is a string rather than
 *  a boolean: a future Mega Drive / genmddj encoder would be a third value, not a second flag. */
export type SmsSyncMachine = "sms" | "gg";

/** The level encoder for `machine`. The one place the two wire formats are chosen between. */
export function syncLevelsFor(machine: SmsSyncMachine): (counter: number) => number {
  return machine === "gg" ? ggSyncLevels : smsSyncLevels;
}
