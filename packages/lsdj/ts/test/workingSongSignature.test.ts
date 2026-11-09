// The working-song signature LSDj's playback hook answers with (lsdjPlayback.ts, workingSongSignature): a poll compares
// it to notice a song edited ON THE CART, so it must move for every edit that changes row timing and for nothing else -
// a false positive decodes the whole battery again, several times a minute, while somebody works. The store's poll and
// its publish are the shell's (test/controller/songRefresh.test.ts).
import { test, expect } from "@retroplug/testing";
import { workingSongSignature } from "../playback/fromSav";
import * as R from "../codec/regions";

/** A blank song bank, big enough to be a plausible battery. */
function sav(): Uint8Array {
  return new Uint8Array(R.kSongByteCount + 0x200);
}

test("the signature moves when a song row's chain changes", () => {
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.chainAssignments + 4 * R.kChannelCount] = 3; // row 4, pu1 -> chain 3
  expect(workingSongSignature(a) !== before).toBe(true);
});

test("the signature moves when a chain gains or loses a phrase", () => {
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.chainPhrases + 3 * R.kChainLength + 1] = 9; // chain 3, slot 1 -> phrase 9
  expect(workingSongSignature(a) !== before).toBe(true);
  const withPhrase = workingSongSignature(a);
  a[R.kModernRegions.chainAllocations] ^= 0x08; // chain 3 allocated/not
  expect(workingSongSignature(a) !== withPhrase).toBe(true);
});

test("the signature moves when groove 0 changes, because that sets the row duration", () => {
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.grooves + 1] = 3;
  expect(workingSongSignature(a) !== before).toBe(true);
});

test("the signature IGNORES edits that cannot change row timing", () => {
  // Renaming a file, tweaking an instrument, writing a phrase note: all real edits, none of which move a
  // single row's duration. Re-pushing for these would decode the whole sav for nothing, several times a
  // minute, while somebody works.
  const a = sav();
  const before = workingSongSignature(a);
  a[R.kModernRegions.instrumentNames + 2] = 0x41;
  a[R.kModernRegions.instrumentParams + 5] = 0x7f;
  a[R.kModernRegions.phraseNotes + 40] = 0x50;
  a[R.kModernRegions.tempo] = 150;
  expect(workingSongSignature(a)).toBe(before);
});

test("an unreadable battery is inert rather than a change", () => {
  // Null / too short / not yet published. Returning a constant means the poll simply never fires, which
  // is what "no song feedback" should look like everywhere else in this feature.
  expect(workingSongSignature(null)).toBe(0);
  expect(workingSongSignature(new Uint8Array(16))).toBe(0);
  expect(workingSongSignature(undefined)).toBe(0);
});
