// genmddj's IN24 level word (design section 4): the same 2-bit counter smsggdj uses, on this console's pins.
//
// genmddj reads it with `move.b $A10005,d0; lsr.b #5,d0; andi.b #3,d0` (src/main.asm:9438): bit 5 is TR, bit 6 is TH,
// and the bit is HIGH when the counter's bit is set. As on every controller port, the ROM reads what the pads drive
// plus its own output latch, so a level word only decides the pins the ROM leaves as inputs ($A1000B's direction).
//
// The other lines idle high, which is what a disconnected pad leaves and what the ROM's arming latches as its base. The
// word below names only the two pins the sync drives; the host's port device answers the rest from its pull-ups, the
// same way smsggdj's word is only the lines its role owns.
//
// The counter itself, and the walk that advances it, are smsggdj's CounterSync (../smsggdj/ts/smsSync.ts); this file is
// the encoder the Mega Drive hands it, and its node is the one that pairs them.
export const GENMDDJ_SYNC_COUNTER_MOD = 4;
export const GENMDDJ_SYNC_IDLE_LEVELS = 0x60;
export const GENMDDJ_SYNC_PPQN = 24;
/** At most 3 clocks survive one of the ROM's frame polls: a 4th aliases to 0 (sync_in_delta's `(read - last) & 3`). */
export const GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL = 3;

const TR_BIT = 0x20; // $A10005 bit 5, the pad's B / the sync's counter bit 0
const TH_BIT = 0x40; // $A10005 bit 6, the pad's C / the sync's counter bit 1

/** The port-2 level word for 2-bit counter value `counter`. */
export function mdSyncLevels(counter: number): number {
  const c = ((counter % GENMDDJ_SYNC_COUNTER_MOD) + GENMDDJ_SYNC_COUNTER_MOD) % GENMDDJ_SYNC_COUNTER_MOD;
  let levels = 0;
  if (c & 1) levels |= TR_BIT;
  if (c & 2) levels |= TH_BIT;
  return levels;
}
