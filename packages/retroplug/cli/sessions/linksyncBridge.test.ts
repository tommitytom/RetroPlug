// Runnable with plain Node (type-stripping): `node --test linksyncBridge.test.ts`.
// Proves the LinkSyncBridge emits the drift-exact 24-PPQN LSDj clock — the same walkTicks the DSP
// `lsdj-sync` role uses, so the hardware byte stream matches the in-plugin one by construction.

import test from "node:test";
import assert from "node:assert/strict";
import { LinkSyncBridge, LsdjSyncModeNum, LSDJ_CLOCK, LSDJ_START, LSDJ_STOP } from "./linksyncBridge.ts";

const block = (ppqStart: number, transport: boolean) => ({
  frames: 44100,
  sampleRate: 44100,
  tempo: 120, // 2 quarter-notes / second
  ppqStart,
  transport,
});

test("MidiSync: 48 clock ticks over 1s @120bpm, divisor 1", () => {
  const b = new LinkSyncBridge();
  const r = b.processBlock(block(0, true), { mode: LsdjSyncModeNum.MidiSync, tempoDivisor: 1, autoStart: false });
  assert.equal(r.events.length, 48); // 2 beats * 24 PPQN
  assert.ok(r.events.every((e) => e.byte === LSDJ_CLOCK));
  assert.equal(r.pressStart, false);
});

test("MidiSync divisor 2 halves the clock (24 ticks)", () => {
  const b = new LinkSyncBridge();
  const r = b.processBlock(block(0, true), { mode: LsdjSyncModeNum.MidiSync, tempoDivisor: 2, autoStart: false });
  assert.equal(r.events.length, 24);
});

test("MidiSync autoStart taps Start on the transport rise only", () => {
  const b = new LinkSyncBridge();
  const cfg = { mode: LsdjSyncModeNum.MidiSync, tempoDivisor: 1, autoStart: true };
  assert.equal(b.processBlock(block(0, true), cfg).pressStart, true); // rise
  assert.equal(b.processBlock(block(2, true), cfg).pressStart, false); // already running
});

test("MidiSync clock is drift-free across two contiguous blocks (48 + 48)", () => {
  const b = new LinkSyncBridge();
  const cfg = { mode: LsdjSyncModeNum.MidiSync, tempoDivisor: 1, autoStart: false };
  const b1 = b.processBlock(block(0, true), cfg);
  const b2 = b.processBlock(block(2, true), cfg); // 1s later, ppq advanced by 2 beats
  assert.equal(b1.events.length, 48);
  assert.equal(b2.events.length, 48); // no gap/overlap at the block edge
});

test("Arduinoboy bookends transport edges with 0xFA / 0xFC", () => {
  const b = new LinkSyncBridge();
  const cfg = { mode: LsdjSyncModeNum.MidiSyncArduinoboy, tempoDivisor: 1, autoStart: false };
  const started = b.processBlock(block(0, true), cfg);
  assert.equal(started.events[0].byte, LSDJ_START); // 0xFA first
  assert.ok(started.events.slice(1).every((e) => e.byte === LSDJ_CLOCK));
  const stopped = b.processBlock(block(2, false), cfg);
  assert.equal(stopped.events.length, 1);
  assert.equal(stopped.events[0].byte, LSDJ_STOP); // 0xFC on the fall
});

test("Off / MidiMap produce no transport-driven bytes", () => {
  const b = new LinkSyncBridge();
  assert.equal(b.processBlock(block(0, true), { mode: LsdjSyncModeNum.Off, tempoDivisor: 1, autoStart: false }).events.length, 0);
  assert.equal(b.processBlock(block(0, true), { mode: LsdjSyncModeNum.MidiMap, tempoDivisor: 1, autoStart: false }).events.length, 0);
});
