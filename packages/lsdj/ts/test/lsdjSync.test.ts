// rp.lsdj.sync (lsdjSyncNode.ts): LSDj's sync modes, host to Game Boy, as the DSP kernel's lsdj-sync role ran them
// until B3b. The cases its kernel-level tests held of every mode that feeds the cart, at the node, with what the
// kernel node's own tests held of the serial-in runs. MI.OUT and master sync, the Game Boy's serial out decoded to
// host MIDI, are rp.lsdj.serial-out's (lsdjSerialOut.test.ts); here they send nothing.
import { test, expect } from "@retroplug/testing";
import { LSDJ_SYNC_NODE_TYPE, lsdjSyncNode } from "../lsdjSyncNode";
import { NodeRig, bytesOn, on, type BlockSpec, type In, type Sent } from "@retroplug/testing/rig";

const lsdj = (mode: string, config: Record<string, unknown> = {}) => new NodeRig(lsdjSyncNode, { mode, ...config });
// 22050 frames @ 44100 / 120 = exactly 1 beat (24 ticks at 24 PPQN).
const beat = (over: Partial<BlockSpec> = {}): BlockSpec => ({ frames: 22050, ...over });
const withMidi = (midi: In[], over: Partial<BlockSpec> = {}): BlockSpec => beat({ ...over, inputs: { midi, ...over.inputs } });
const noteOn = (channel: number, note: number, frame = 0): In => ({ frame, data: [0x90 | channel, note, 100] });
const noteOff = (channel: number, note: number, frame = 0): In => ({ frame, data: [0x80 | channel, note, 0] });
const serial = (sent: Sent[]): number[] => bytesOn(sent, "serialIn").map(([, b]) => b);
const clocks = (sent: Sent[]): number => serial(sent).filter((b) => b === 0xf8).length;

test("its ports: the routed MIDI and the launches in; serial in and buttons out", () => {
  expect(lsdj("midiSync").ports()).toEqual(["in events midi", "in events launches", "out bytes serialIn", "out events buttons"]);
  expect(lsdjSyncNode.descriptor().id).toBe(LSDJ_SYNC_NODE_TYPE);
  const caps = lsdjSyncNode.layout(lsdjSyncNode.parseSettings({})).ports.slice(2).map((p) => p.capacity);
  expect(caps).toEqual([{ messages: 4096, bytes: 73728 }, { messages: 64, bytes: 128 }]);
  expect(lsdjSyncNode.reshapes).toBe(true);
});

test("settings default as the role's schema does, and refuse what it would clamp", () => {
  expect(lsdjSyncNode.parseSettings({})).toEqual({ mode: "midiSync", tempoDivisor: 1, autoStart: false });
  expect(() => lsdjSyncNode.parseSettings({ mode: "sync" })).toThrow("mode: Invalid option");
  expect(() => lsdjSyncNode.parseSettings({ tempoDivisor: 9 })).toThrow("tempoDivisor: Too big");
  expect(() => lsdjSyncNode.parseSettings({ autoStart: 1 })).toThrow("autoStart: Invalid input");
});

test("MidiSync emits a 24-PPQN 0xF8 clock at its ticks' frames; Off emits nothing", () => {
  const on = lsdj("midiSync").run(beat({ playing: true }));
  expect(clocks(on)).toBe(24);
  expect(bytesOn(on, "serialIn").slice(0, 3)).toEqual([[0, 0xf8], [918, 0xf8], [1837, 0xf8]]);
  expect(lsdj("off").run(beat({ playing: true }))).toEqual([]);
  expect(lsdj("keyboard").run(withMidi([noteOn(0, 60)], { playing: true }))).toEqual([]);
});

test("MidiSync's tempo divisor subdivides the clock, and its auto-arm taps START on the transport's rise", () => {
  expect(clocks(lsdj("midiSync", { tempoDivisor: 4 }).run(beat({ playing: true })))).toBe(6);
  const rig = lsdj("midiSync", { autoStart: true });
  const press = (sent: Sent[]) => on(sent, "buttons");
  expect(press(rig.run({ frames: 1024 }))).toEqual([]);
  expect(press(rig.run({ frames: 1024, playing: true }))).toEqual([[0, 7, 1]]); // pressed for 2 blocks...
  expect(press(rig.run({ frames: 1024, playing: true, ppq: 0.05 }))).toEqual([]);
  expect(press(rig.run({ frames: 1024, playing: true, ppq: 0.1 }))).toEqual([[0, 7, 0]]); // ...then released
  expect(press(rig.run({ frames: 1024, playing: true, ppq: 0.15 }))).toEqual([]);
});

test("an auto-arm turned on while the transport plays reads the edge it last saw with it on", () => {
  // The arm's memory of the transport moves only while the arm is on, as the kernel's did: switched off across a rise
  // and on again mid-play, it still remembers the transport stopped, so it taps START on its first block back.
  const rig = lsdj("midiSync", { autoStart: true });
  const press = (sent: Sent[]) => on(sent, "buttons");
  expect(press(rig.run({ frames: 1024 }))).toEqual([]); // seen stopped, with it on
  expect(rig.set({ mode: "midiSync", autoStart: false })).toBe(true);
  expect(press(rig.run({ frames: 1024, playing: true }))).toEqual([]); // the rise, with it off: not seen
  rig.run({ frames: 1024, playing: true });
  expect(rig.set({ mode: "midiSync", autoStart: true })).toBe(true);
  expect(press(rig.run({ frames: 1024, playing: true }))).toEqual([[0, 7, 1]]);
  // A node that has never seen the transport with it on takes the same first block as a rise.
  const fresh = lsdj("midiSync");
  fresh.run({ frames: 1024, playing: true });
  fresh.set({ mode: "midiSync", autoStart: true });
  expect(press(fresh.run({ frames: 1024, playing: true }))).toEqual([[0, 7, 1]]);
});

test("MidiPassthrough forwards raw MIDI bytes verbatim to serial", () => {
  expect(serial(lsdj("midiPassthrough").run(withMidi([noteOn(0, 60)])))).toEqual([0x90, 60, 100]);
});

test("MidiMap: ch0 NoteOn → row byte, matching NoteOff → 0xFE, ch1 → row+128 (lastRow persists across blocks)", () => {
  const rig = lsdj("midiMap");
  expect(serial(rig.run(withMidi([noteOn(0, 5)])))).toEqual([5]); // ch0 note 5 → row 5
  expect(serial(rig.run(withMidi([noteOff(0, 5)])))).toEqual([0xfe]); // matching off → sentinel
  expect(serial(rig.run(withMidi([noteOff(0, 9)])))).toEqual([]); // non-matching off → nothing
  expect(serial(rig.run(withMidi([noteOn(1, 5)])))).toEqual([133]); // ch1 note 5 → 5 + 128
  expect(serial(rig.run(withMidi([noteOn(2, 5)])))).toEqual([]); // other channels are no row
});

test("MidiMap clocks the cart with 0xFF at frame 0 while the transport plays, after the rows", () => {
  const sent = lsdj("midiMap").run(withMidi([noteOn(0, 3, 500)], { playing: true }));
  const got = bytesOn(sent, "serialIn");
  expect(got[0]).toEqual([500, 3]);
  // Each clock is sent at frame 0, which the queue raises to the row's frame.
  expect(got.slice(1).every(([f, b]) => f === 500 && b === 0xff)).toBe(true);
  expect(got.length).toBe(25);
});

test("MidiSyncArduinoboy: note 24 arms the clock behind a 0xFA bookend; note 25 stops it with 0xFC", () => {
  const rig = lsdj("midiSyncArduinoboy");
  expect(rig.run(beat())).toEqual([]); // idle: not playing, transport off
  // Note 24 arms play; transport rises this block → 0xFA first, then a full 24-tick clock (divisor 1).
  const started = rig.run(withMidi([noteOn(0, 24)], { playing: true }));
  expect(serial(started)[0]).toBe(0xfa);
  expect(clocks(started)).toBe(24);
  // Still playing, transport already true → clock continues drift-free, no new bookend.
  const cont = rig.run(beat({ ppq: 1, playing: true }));
  expect(serial(cont).includes(0xfa)).toBeFalsy();
  expect(clocks(cont)).toBe(24);
  // Note 25 stops play; transport falls → a lone 0xFC bookend, no clock.
  expect(serial(rig.run(withMidi([noteOn(0, 25)], { ppq: 2 })))).toEqual([0xfc]);
});

test("MidiSyncArduinoboy: tempo divisor subdivides the clock; note >= 30 pushes a raw row byte", () => {
  expect(clocks(lsdj("midiSyncArduinoboy", { tempoDivisor: 2 }).run(withMidi([noteOn(0, 24)], { playing: true })))).toBe(12);
  // note 40 → 40 - 30 = 10 (row passthrough is independent of the play flag).
  expect(serial(lsdj("midiSyncArduinoboy").run(withMidi([noteOn(0, 40)])))).toEqual([10]);
  // Notes 26-29 set the divisor (1, 2, 4, 8), which outlives a tempoDivisor edit, as the role's did.
  const rig = lsdj("midiSyncArduinoboy");
  rig.run(withMidi([noteOn(0, 24), noteOn(0, 28)], { playing: true }));
  expect(rig.set({ mode: "midiSyncArduinoboy", tempoDivisor: 2 })).toBe(true);
  expect(clocks(rig.run(beat({ ppq: 1, playing: true })))).toBe(6);
});

test("KeyboardMidi: a note maps to its GB-serial-mangled PS/2 scancode; a cursor note gets the extended prefix", () => {
  const rig = lsdj("keyboardMidi");
  // note 48 (C-3 = NOTE_START): octave slides 4 → 0 (4× OCT_DN 0x05 → 0x50) then NOTE_MAP[0] 0x1A → 0x2C.
  expect(serial(rig.run(withMidi([noteOn(0, 48)])))).toEqual([0x50, 0x50, 0x50, 0x50, 0x2c]);
  // note 40 (LOW_START 36 + 4 = Cursor Left 0x6B) → extended prefix 0xE0 → 0x03, then 0x6B → 0x6B.
  expect(serial(rig.run(withMidi([noteOn(0, 40)])))).toEqual([0x03, 0x6b]);
});

test("the inbox is the routed MIDI, then the launches: a launch at frame 0 goes after a routed row at a later frame", () => {
  const sent = lsdj("midiMap").run({
    frames: 1024,
    inputs: { midi: [noteOn(0, 7, 600)], launches: [{ frame: 0, data: [0x90, 2, 100] }, { frame: 0, data: [0x80, 2, 0] }] },
  });
  // Row 7 at 600, then the launch's row 2 and its release's handshake, each raised to 600 by the queue.
  expect(bytesOn(sent, "serialIn")).toEqual([[600, 7], [600, 2], [600, 0xfe]]);
  expect(sent.map((s) => s.frame)).toEqual([600, 0]); // two runs: the row's, then the launch's at frame 0
});

test("serial-in bytes go a run of same-frame bytes to a message, each run sent before the next frame's", () => {
  // Arduinoboy, on the transport's rise: each note's row byte at its frame, then START at frame 0, a run of its own.
  const sent = lsdj("midiSyncArduinoboy").run({
    frames: 1024,
    playing: true,
    inputs: { midi: [noteOn(0, 30, 9), noteOn(0, 31, 9), noteOn(0, 40, 200)] },
  });
  expect(sent.filter((s) => s.port === "serialIn").map((s) => [s.frame, ...s.data])).toEqual([[9, 0, 1], [200, 10], [0, 0xfa]]);
});

test("a run a throw leaves is sent, and a run a stopped call left is never sent in a later block", () => {
  // No mode throws on what it accepts, so the block throws: a second routed message whose data cannot be read, behind
  // a first the node has already pushed to serial as the run it holds.
  const rig = lsdj("midiPassthrough");
  const poisoned = rig.node;
  const real = poisoned.process.bind(poisoned);
  poisoned.process = (b) => {
    const messages = b.messages;
    b.messages = (port) => {
      const list = messages.call(b, port);
      if (port !== "midi") return list;
      const bad = { frame: 9, flags: 0, get data(): Uint8Array { throw new Error("injected"); } };
      return [...list, bad];
    };
    return real(b);
  };
  expect(() => rig.run(withMidi([noteOn(0, 3, 4)], { frames: 1024 }))).toThrow("injected");
  poisoned.process = real;
  expect(rig.last.map((s) => [s.port, s.frame, ...s.data])).toEqual([["serialIn", 4, 0x90, 3, 100]]);

  // The deadline's stop skips the finally that sends the run. A send that throws stands in for it: the run is still
  // held when the call ends, and the next block starts from none.
  const stopped = lsdj("midiPassthrough");
  const run = stopped.run.bind(stopped);
  const node = stopped.node;
  const process = node.process.bind(node);
  node.process = (b) => {
    const send = b.send;
    b.send = () => {
      throw new Error("stopped");
    };
    try {
      return process(b);
    } finally {
      b.send = send;
    }
  };
  expect(() => run(withMidi([noteOn(0, 60, 4)], { frames: 1024 }))).toThrow("stopped");
  node.process = process;
  expect(run(withMidi([noteOn(0, 61, 7)], { frames: 1024 })).map((s) => [s.frame, ...s.data])).toEqual([[7, 0x90, 61, 100]]);
});

// --- MI.OUT (mode 7) and master sync (mode 8): rp.lsdj.serial-out's -------------------------------------------------

test("MI.OUT and master sync send nothing: the Game Boy's serial out is decoded after the Game Boy, not here", () => {
  // The routed MIDI and the transport each mode could act on, reaching neither the cart nor the host.
  for (const mode of ["midiOut", "masterSync"]) {
    const rig = lsdj(mode, { autoStart: true });
    expect(rig.run(withMidi([noteOn(0, 24), noteOn(0, 40, 9)], { playing: true })), mode).toEqual([]);
    expect(rig.run(beat({ ppq: 1, playing: true })), mode).toEqual([]);
  }
});

// --- state across settings changes ---------------------------------------------------------------------------------

test("a settings change keeps the clock: a mode change mid-run does not restart the tick count", () => {
  const rig = lsdj("midiSync");
  expect(clocks(rig.run(beat({ playing: true })))).toBe(24);
  rig.set({ mode: "midiMap" });
  // ppq stays 0, so a fresh counter would resync and send the same 24; one that kept its counter is past this quarter.
  expect(serial(rig.run(beat({ playing: true }))).length).toBe(0);
  expect(rig.creates).toBe(1);
});

test("a fresh instance starts fresh: the play flag clear, and the transport's first block a rising edge", () => {
  // The kernel pruned a removed system's scratch, so a re-added one started fresh; a new node is fresh by being new.
  const old = lsdj("midiSyncArduinoboy");
  old.run(withMidi([noteOn(0, 24)], { playing: true }));
  const out = lsdj("midiSyncArduinoboy").run(beat({ playing: true }));
  expect(clocks(out)).toBe(0);
  expect(serial(out).includes(0xfa)).toBeTruthy();
});
