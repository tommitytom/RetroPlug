// Runnable with plain Node: `node --test linksync.test.ts`.
// Covers the deterministic sync-script generation + rpsync formatting (the host-bridge output that the
// Chromatic MCU consumes). Type-only imports of ../tools and ../session are erased, so this loads under
// Node without the CLI runtime.

import test from "node:test";
import assert from "node:assert/strict";
import { generateSyncScript, formatRpsync, parseLinkSyncArgs } from "./linksync.ts";
import { LsdjSyncModeNum } from "./linksyncBridge.ts";

test("formatRpsync builds the MCU console command line", () => {
  assert.equal(formatRpsync(LsdjSyncModeNum.MidiSync, [0xf8, 0xf8]), "rpsync 1 f8 f8");
});

test("generateSyncScript emits rpsync lines totalling the expected 24-PPQN clock count", () => {
  const lines = generateSyncScript({
    bpm: 120, divisor: 1, mode: LsdjSyncModeNum.MidiSync,
    durationMs: 1000, blockMs: 20, autoStart: false, sampleRate: 44100,
  });
  // Every emitted line is an rpsync command.
  assert.ok(lines.length > 0);
  assert.ok(lines.every((l) => l.startsWith("rpsync 1 ")));
  // ~48 0xF8 ticks over 1s @120bpm (2 beats * 24 PPQN). The boundary tick at ppq 2.0 may or may not
  // land in the last block depending on float rounding of framePpqEnd — this is walkTicks' own
  // behavior, identical to the plugin, so accept 48 or 49 (the point is the shared drift-exact clock).
  const clocks = lines.reduce((n, l) => n + l.split(" ").slice(2).filter((b) => b === "f8").length, 0);
  assert.ok(clocks === 48 || clocks === 49, `expected 48/49 clocks, got ${clocks}`);
});

test("generateSyncScript with --auto-start emits a leading poke (Start)", () => {
  const lines = generateSyncScript({
    bpm: 120, divisor: 1, mode: LsdjSyncModeNum.MidiSync,
    durationMs: 200, blockMs: 20, autoStart: true, sampleRate: 44100,
  });
  assert.equal(lines[0], "poke 8"); // Start pressed on the transport rise
});

test("parseLinkSyncArgs parses flags and rejects unknowns", () => {
  const o = parseLinkSyncArgs(["--bpm", "140", "--mode", "arduinoboy", "--duration", "2s", "--divisor", "2"]);
  assert.equal(o.bpm, 140);
  assert.equal(o.mode, LsdjSyncModeNum.MidiSyncArduinoboy);
  assert.equal(o.durationMs, 2000);
  assert.equal(o.divisor, 2);
  assert.throws(() => parseLinkSyncArgs(["--nope"]));
});
