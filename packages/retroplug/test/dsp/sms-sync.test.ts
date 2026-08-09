// The sms-sync DSP role driven through the kernel: it turns the DAW transport into smsggdj's
// controller-port sync transport - a 2-bit counter held on port 2's TR + TH lines at 24 PPQN. NOT a
// byte protocol; each payload is a LEVEL WORD the native side holds on the port until the next one.
// Mirrors dsp/risa-sync.test.ts. Pure TS, no backend.
//
// Protocol reference: smsggdj's GGSYNC.md and src/engine.asm:576-590 / :646-661.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type BlockInput } from "../../src/dspKernel";
import { registerRomProviders } from "../../src/romProviders";
import {
  isSmsggdjRom,
  smsSyncLevels,
  SMS_SYNC_IDLE_LEVELS,
  SMS_SYNC_COUNTER_MOD,
  SMS_SYNC_MAX_CLOCKS_PER_POLL,
  SMS_SYNC_PPQN,
} from "../../src/smsSync";

// A one-system project carrying just the sms-sync role.
function sms(): DspKernel {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  const k = new DspKernel(reg);
  k.setSystems({ project: [], systems: [{ id: 1, pipeline: [{ kind: "sms-sync", config: {} }] }] });
  return k;
}

// 22050 frames @ 44100 / 120 bpm = exactly 1 beat (24 ticks at 24 PPQN).
const baseDyn = (): BlockInput => ({
  frames: 22050, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false, midiIn: [], buttons: [], keys: [], serialOut: [],
});
type Out = { coreBytes: { frame: number; data: number[]; flush?: boolean }[] };
const levels = (out: Out): number[] => out.coreBytes.map((e) => e.data[0]);

// --- the encoding ------------------------------------------------------------

test("smsSyncLevels: counter bits are active HIGH, so a zero bit PULLS ITS LINE LOW", () => {
  // The direction here is the one thing that cannot be got wrong quietly: inverting it still produces
  // four distinct words, so the ROM still sees a changing counter - it would just run backwards.
  expect(smsSyncLevels(0)).toBe(0x77); // TR low, TH low
  expect(smsSyncLevels(1)).toBe(0x7f); // TR high, TH low
  expect(smsSyncLevels(2)).toBe(0xf7); // TR low, TH high
  expect(smsSyncLevels(3)).toBe(0xff); // both high == the idle word
  expect(smsSyncLevels(3)).toBe(SMS_SYNC_IDLE_LEVELS);
});

test("smsSyncLevels: TL (bit 2) is never pulled low, so the ROM's 'TR AND TL' reduces to TR", () => {
  for (let c = 0; c < 8; c++) expect(smsSyncLevels(c) & 0x04).toBe(0x04);
});

test("smsSyncLevels: wraps mod 4, including negative input", () => {
  for (let c = 0; c < 12; c++) expect(smsSyncLevels(c)).toBe(smsSyncLevels(c % SMS_SYNC_COUNTER_MOD));
  expect(smsSyncLevels(-1)).toBe(smsSyncLevels(3));
  expect(smsSyncLevels(-4)).toBe(smsSyncLevels(0));
});

// --- the role ----------------------------------------------------------------

test("idle (transport off) emits nothing", () => {
  expect(sms().processBlock({ ...baseDyn() }).coreBytes.length).toBe(0);
});

test("transport emits one level per 24-PPQN tick, no arm and no start message", () => {
  const out = sms().processBlock({ ...baseDyn(), transport: true }) as Out;
  // 24, not 23: unlike risa there is no armed clock the ROM primes itself, so none is withheld.
  expect(out.coreBytes.length).toBe(SMS_SYNC_PPQN);
  // Every payload is a single level word - nothing that could be mistaken for a protocol byte.
  for (const e of out.coreBytes) expect(e.data.length).toBe(1);
});

test("the counter ADVANCES one step per tick and wraps - the ROM reads deltas, not absolutes", () => {
  const out = sms().processBlock({ ...baseDyn(), transport: true }) as Out;
  const want: number[] = [];
  for (let i = 1; i <= SMS_SYNC_PPQN; i++) want.push(smsSyncLevels(i));
  expect(levels(out)).toEqual(want);
});

test("no level is ever repeated back-to-back, or the ROM would read a delta of zero", () => {
  // This is the failure mode that looks like working code: a role that pushed a constant would still
  // emit the right NUMBER of events at the right offsets, and the song would simply never advance.
  const seq = levels(sms().processBlock({ ...baseDyn(), transport: true }) as Out);
  const repeats = seq.filter((v, i) => i > 0 && v === seq[i - 1]).length;
  expect(repeats).toBe(0);
});

test("the counter carries across blocks rather than restarting", () => {
  const k = sms();
  const a = levels(k.processBlock({ ...baseDyn(), transport: true }) as Out);
  const b = levels(k.processBlock({ ...baseDyn(), transport: true, ppqStart: 1 }) as Out);
  // The first tick of block 2 must still step the counter - restarting it would repeat a level across
  // the boundary and cost the ROM a beat exactly once per block, which is easy to miss by ear.
  expect(b[0] === a[a.length - 1]).toBeFalsy();
  expect(b[0]).toBe(smsSyncLevels(SMS_SYNC_PPQN + 1));
});

test("no flush is ever requested - a held level has no undelivered-stream hazard", () => {
  // risa's arm IS a barrier (a stale byte after a re-locate corrupts the stream); this transport has
  // no equivalent, and SmsSyncRole accepts-and-ignores the flag. Asserting it here keeps the two
  // protocols from being conflated later.
  const out = sms().processBlock({ ...baseDyn(), transport: true }) as Out;
  for (const e of out.coreBytes) expect(e.flush).toBeFalsy();
});

test("stop is implicit: clocks simply cease, with no stop message", () => {
  const k = sms();
  k.processBlock({ ...baseDyn(), transport: true });
  const out = k.processBlock({ ...baseDyn(), transport: false, ppqStart: 1 }) as Out;
  expect(out.coreBytes.length).toBe(0); // not "one stop byte" - nothing at all
});

test("a seek does NOT relocate: the role is positionless by design", () => {
  // risa re-arms on a ppqStart discontinuity. This protocol has no locate, so a jump must produce the
  // same steady clock stream - the DAW drives tempo/start/stop but not position.
  const k = sms();
  k.processBlock({ ...baseDyn(), transport: true });
  const out = k.processBlock({ ...baseDyn(), transport: true, ppqStart: 64 }) as Out; // big jump
  expect(out.coreBytes.length).toBe(SMS_SYNC_PPQN);
  for (const e of out.coreBytes) expect(e.data.length).toBe(1); // no arm packet appeared
});

test("ticks land at spread sample offsets, not bunched at block start", () => {
  // The offsets are what the whole native slice exists to honour; a role that emitted everything at
  // frame 0 would still pass every count-based assertion above.
  const frames = (sms().processBlock({ ...baseDyn(), transport: true }) as Out).coreBytes.map((e) => e.frame);
  expect(new Set(frames).size).toBe(frames.length); // strictly distinct
  expect(frames.filter((f, i) => i > 0 && f <= frames[i - 1]).length).toBe(0); // strictly ascending
  expect(frames.filter((f) => f < 0 || f >= 22050).length).toBe(0); // all inside the block
});

test("the documented aliasing ceiling matches the tempo the role can actually clock", () => {
  // The ROM recovers (current - last) & 3 once per video frame, so >3 clocks between polls loses a
  // beat. Derived here rather than asserted as a magic number, so a PPQN change moves it.
  const ntscFps = 59.9227434;
  const maxBpm = (SMS_SYNC_MAX_CLOCKS_PER_POLL * ntscFps * 60) / SMS_SYNC_PPQN;
  expect(Math.round(maxBpm)).toBe(449);
  // At the ceiling a block still yields more clocks than a beat's worth - the limit is the ROM's poll
  // rate, not anything in the role, so nothing here should throttle.
  const out = sms().processBlock({ ...baseDyn(), transport: true, tempo: 449 }) as Out;
  expect(out.coreBytes.length > SMS_SYNC_PPQN).toBeTruthy();
});

// --- attachment ---------------------------------------------------------------

test("isSmsggdjRom finds the build marker, and only that", () => {
  const rom = new Uint8Array(0x20000);
  expect(isSmsggdjRom(rom)).toBeFalsy(); // blank
  const mark = (at: number) => {
    const b = new Uint8Array(0x20000);
    for (let i = 0; i < "SMSGGDJ".length; i++) b[at + i] = "SMSGGDJ".charCodeAt(i);
    return b;
  };
  expect(isSmsggdjRom(mark(0x3640))).toBeTruthy(); // where v0.45 actually puts it
  expect(isSmsggdjRom(mark(0))).toBeTruthy(); // position is not assumed
  expect(isSmsggdjRom(mark(0x7ff0))).toBeTruthy();
  // A near-miss must not match, or every Sega ROM with "SMS" in it would pick up the role.
  const near = mark(0x1000);
  near[0x1003] = 0x00; // "SMS\0GDJ"
  expect(isSmsggdjRom(near)).toBeFalsy();
});

test("the marker must be inside the prefix the caller actually read", () => {
  // ROLE_HEADER_LEN (0x150) cannot reach $3640, which is the whole reason defaultRoles reads
  // SEGA_SNIFF_LEN for sms/gg. A short buffer declines rather than throwing.
  const b = new Uint8Array(0x20000);
  for (let i = 0; i < "SMSGGDJ".length; i++) b[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  expect(isSmsggdjRom(b.subarray(0, 0x150))).toBeFalsy();
  expect(isSmsggdjRom(b.subarray(0, 0x8200))).toBeTruthy();
});

test("the provider attaches sms-sync to smsggdj only, and never to Game Gear", () => {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  registerRomProviders(reg);
  const kinds = (platform: string, header: Uint8Array) =>
    reg.defaultRoles("mesen" as never, platform as never, header, "").map((r) => r.kind);

  const marked = new Uint8Array(0x8200);
  for (let i = 0; i < "SMSGGDJ".length; i++) marked[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  const plain = new Uint8Array(0x8200);

  expect(kinds("sms", marked).includes("sms-sync")).toBeTruthy();
  // A generic SMS cart gets nothing: the transport drives Player 2's button lines, so attaching it
  // unconditionally would press buttons in someone's game.
  expect(kinds("sms", plain).includes("sms-sync")).toBeFalsy();
  // Game Gear is excluded even WITH the marker - its build reads the EXT port, which stock Mesen
  // loopbacks. A role that silently did nothing would be worse than no role at all.
  expect(kinds("gg", marked).includes("sms-sync")).toBeFalsy();
  // And no other platform picks it up.
  expect(kinds("nes", marked).includes("sms-sync")).toBeFalsy();
  expect(kinds("gb", marked).includes("sms-sync")).toBeFalsy();
});
