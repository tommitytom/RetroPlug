// smsggdj's sync transport (src/smsSync.ts): a 2-bit counter held on two port lines at 24 PPQN, each step
// a LEVEL WORD the native side holds on the port until the next one. The encodings for BOTH machines -
// Master System on controller port 2's TR + TH, Game Gear on the EXT port's PC4/PC5 + PC6 - and the ROM
// provider that attaches the sms-sync role. The rp.sms.sync node that clocks the counter from the DAW
// transport is test/script/nodes/sms-sync.test.ts's. Pure TS, no backend.
//
// Protocol reference: smsggdj's GGSYNC.md and src/engine.asm:576-590 / :646-661.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerRomProviders } from "../../src/romProviders";
import {
  isSmsggdjRom,
  smsSyncLevels,
  ggSyncLevels,
  SMS_SYNC_IDLE_LEVELS,
  GG_SYNC_IDLE_LEVELS,
  SMS_SYNC_COUNTER_MOD,
} from "../../src/smsSync";

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

test("the provider attaches sms-sync to smsggdj only, on both machines", () => {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  registerRomProviders(reg);
  const kinds = (platform: string, header: Uint8Array) =>
    reg.defaultRoles("mesen" as never, platform as never, header, "").map((r) => r.kind);

  const marked = new Uint8Array(0x8200);
  for (let i = 0; i < "SMSGGDJ".length; i++) marked[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  const plain = new Uint8Array(0x8200);

  expect(kinds("sms", marked).includes("sms-sync")).toBeTruthy();
  // Game Gear too - the marker is in the shared source, so the .gg build carries it.
  expect(kinds("gg", marked).includes("sms-sync")).toBeTruthy();
  // A generic cart gets nothing on either machine: the SMS transport drives Player 2's button lines,
  // so attaching it unconditionally would press buttons in someone's game.
  expect(kinds("sms", plain).includes("sms-sync")).toBeFalsy();
  expect(kinds("gg", plain).includes("sms-sync")).toBeFalsy();
  // And no other platform picks it up.
  expect(kinds("nes", marked).includes("sms-sync")).toBeFalsy();
  expect(kinds("gb", marked).includes("sms-sync")).toBeFalsy();
});

test("the provider tags each machine with its own wire format", () => {
  // The role is one protocol with two encodings, so the provider has to say WHICH - and get it from
  // the platform rather than defaulting. A `.gg` tagged "sms" would drive $DD's bit layout onto the
  // EXT port: pins the ROM does not read, so it would arm and then sit in WAIT forever.
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  registerRomProviders(reg);
  const marked = new Uint8Array(0x8200);
  for (let i = 0; i < "SMSGGDJ".length; i++) marked[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  const machineOf = (platform: string) =>
    (reg.defaultRoles("mesen" as never, platform as never, marked, "")
      .find((r) => r.kind === "sms-sync")?.config as { machine?: string } | undefined)?.machine;

  expect(machineOf("sms")).toBe("sms");
  expect(machineOf("gg")).toBe("gg");
});

// --- Game Gear: the same counter on different pins ---------------------------

test("ggSyncLevels: counter bit 0 drives PC4 AND PC5, bit 1 drives PC6", () => {
  // GGSYNC.md's parallel-counter contract. PC4 and PC5 move TOGETHER because the ROM reads that bit
  // as `PC4 AND PC5`, which is what makes both a direct bridge (PC4 open, pull-up high) and a stock
  // crossed Gear-to-Gear cable decode the same value. Driving only one would work here and then
  // behave differently on hardware depending on the cable.
  expect(ggSyncLevels(0)).toBe(0x0f); // both bits 0: PC4+PC5 low, PC6 low
  expect(ggSyncLevels(1)).toBe(0x3f); // bit 0 set:   PC4+PC5 high, PC6 low
  expect(ggSyncLevels(2)).toBe(0x4f); // bit 1 set:   PC4+PC5 low, PC6 high
  expect(ggSyncLevels(3)).toBe(0x7f); // both set:    all high = idle
  expect(ggSyncLevels(3)).toBe(GG_SYNC_IDLE_LEVELS);
});

test("ggSyncLevels never touches a pin outside PC4-PC6", () => {
  // PC0-PC3 are other people's pins (and bit 7 is not a pin at all). Anything this role clears
  // outside its three would be a phantom signal on a link cable.
  const outside = 0xff & ~0x70;
  for (let c = 0; c < 8; c++) expect(ggSyncLevels(c) & outside).toBe(GG_SYNC_IDLE_LEVELS & outside);
});

test("ggSyncLevels wraps mod 4 and tolerates negative counters", () => {
  for (let c = 0; c < 12; c++) expect(ggSyncLevels(c)).toBe(ggSyncLevels(c % SMS_SYNC_COUNTER_MOD));
  expect(ggSyncLevels(-1)).toBe(ggSyncLevels(3));
  expect(ggSyncLevels(-4)).toBe(ggSyncLevels(0));
});

test("the two machines carry the SAME counter sequence on different pins", () => {
  // The property that justifies one role rather than two: identical state machine, identical delta
  // sequence, only the bit positions differ. If these ever diverge, the shared `machine` config is
  // the wrong abstraction and they should split.
  const bits = (levels: number, lo: number, hi: number) =>
    ((levels & lo) !== 0 ? 1 : 0) | ((levels & hi) !== 0 ? 2 : 0);
  for (let c = 0; c < 4; c++) {
    expect(bits(smsSyncLevels(c), 0x08, 0x80)).toBe(c); // SMS: TR, TH
    expect(bits(ggSyncLevels(c), 0x20, 0x40)).toBe(c); //  GG: PC5, PC6
  }
});
