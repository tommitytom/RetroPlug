// The MIDI routing decision (pure port of Project::dispatchMidi). Locks each mode's
// target selection, the system/SysEx broadcast override, the MidiChannelToInstance
// channel rewrite, the edge cases (n=0/1, size-0), and the routeBlock fan-out.
import { test, expect } from "../../testing/harness";
import { MidiRouting, routeEvent, routeBlock, type MidiEvent } from "../../src/midiRouting";

// A 3-byte note-on on `chan` (status 0x90|chan) → deterministic bytes for assertions.
const noteOn = (chan: number, frame = 0): MidiEvent => ({ frame, data: [0x90 | chan, 60, 100] });

test("SendToAll: every system gets the event, channel preserved", () => {
  const r = routeEvent(noteOn(5), MidiRouting.SendToAll, 3);
  expect(r.targets).toEqual([0, 1, 2]);
  expect(r.data).toEqual([0x95, 60, 100]);
});

test("FourChannelsPerInstance: channels group by 4 into instances (mod n)", () => {
  const t = (c: number) => routeEvent(noteOn(c), MidiRouting.FourChannelsPerInstance, 3).targets;
  expect(t(0)).toEqual([0]);
  expect(t(3)).toEqual([0]);
  expect(t(4)).toEqual([1]);
  expect(t(8)).toEqual([2]);
  expect(t(12)).toEqual([0]); // floor(12/4)=3, 3%3=0
  expect(t(15)).toEqual([0]); // floor(15/4)=3, 3%3=0
});

test("OneChannelPerInstance: channel mod n selects the instance", () => {
  const t = (c: number) => routeEvent(noteOn(c), MidiRouting.OneChannelPerInstance, 3).targets;
  expect(t(0)).toEqual([0]);
  expect(t(3)).toEqual([0]);
  expect(t(4)).toEqual([1]);
  expect(t(2)).toEqual([2]);
});

test("MidiChannelToInstance: channel mod n selects, and the channel nibble is rewritten to 0", () => {
  const r = routeEvent(noteOn(5), MidiRouting.MidiChannelToInstance, 3);
  expect(r.targets).toEqual([2]); // 5 % 3
  expect(r.data).toEqual([0x90, 60, 100]); // 0x95 & 0xf0 = 0x90 (channel 1)
});

test("system/realtime messages broadcast to all, ignoring the routing mode", () => {
  const clock: MidiEvent = { frame: 10, data: [0xf8] }; // status & 0xf0 === 0xf0
  const r = routeEvent(clock, MidiRouting.OneChannelPerInstance, 3);
  expect(r.targets).toEqual([0, 1, 2]);
  expect(r.data).toEqual([0xf8]);
});

test("SysEx (size > 4) broadcasts to all, unchanged, even under a per-channel mode", () => {
  const sysex: MidiEvent = { frame: 0, data: [0x90, 1, 2, 3, 4] }; // length 5 > 4
  const r = routeEvent(sysex, MidiRouting.OneChannelPerInstance, 3);
  expect(r.targets).toEqual([0, 1, 2]);
  expect(r.data).toEqual([0x90, 1, 2, 3, 4]);
});

test("edges: n=1 delivers to [0] for every mode; n=0 and a size-0 event yield no targets", () => {
  for (const m of [
    MidiRouting.SendToAll,
    MidiRouting.FourChannelsPerInstance,
    MidiRouting.OneChannelPerInstance,
    MidiRouting.MidiChannelToInstance,
  ]) {
    expect(routeEvent(noteOn(7), m, 1).targets).toEqual([0]);
  }
  expect(routeEvent(noteOn(7), MidiRouting.SendToAll, 0).targets).toEqual([]);
  expect(routeEvent({ frame: 0, data: [] }, MidiRouting.SendToAll, 3).targets).toEqual([]);
});

test("routeBlock: fans a mixed block into per-system inboxes (skips size-0)", () => {
  const events: MidiEvent[] = [
    noteOn(0, 0), // OneChannel → sys 0
    noteOn(4, 1), // OneChannel → sys 1
    { frame: 2, data: [] }, // size-0 → skipped
    { frame: 3, data: [0xf8] }, // clock → all
  ];
  const inboxes = routeBlock(events, MidiRouting.OneChannelPerInstance, 3);
  expect(inboxes.length).toBe(3);
  expect(inboxes[0]).toEqual([{ frame: 0, data: [0x90, 60, 100] }, { frame: 3, data: [0xf8] }]);
  expect(inboxes[1]).toEqual([{ frame: 1, data: [0x94, 60, 100] }, { frame: 3, data: [0xf8] }]);
  expect(inboxes[2]).toEqual([{ frame: 3, data: [0xf8] }]);
});

test("routeBlock: a MidiChannelToInstance rewrite lands only on the target inbox", () => {
  const inboxes = routeBlock([noteOn(5)], MidiRouting.MidiChannelToInstance, 3);
  expect(inboxes[0]).toEqual([]);
  expect(inboxes[1]).toEqual([]);
  expect(inboxes[2]).toEqual([{ frame: 0, data: [0x90, 60, 100] }]); // 5%3=2, channel→0
});
