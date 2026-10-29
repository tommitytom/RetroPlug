// rp.lsdj.serial-out (src/script/nodes/lsdjSerialOut.ts): the lsdj-sync role's Game Boy-to-host direction, placed
// after its Game Boy, which decodes the serial out the core captured in the same block into host MIDI: MI.OUT (mode 7)
// and master sync (mode 8), each message at the frame the decoder gives it in that block: the MI.OUT and master-sync
// cases rp.lsdj.sync's tests held, here at each message's frame, and what the node keeps across a mode change. The
// decoders' pure cases are test/dsp/lsdj-arduinoboy.test.ts's.
import { test, expect } from "../../../testing/harness";
import { LSDJ_SERIAL_OUT_NODE_TYPE, lsdjSerialOutNode } from "../../../src/script/nodes/lsdjSerialOut";
import { lsdjSyncNode } from "../../../src/script/nodes/lsdjSync";
import { LSDJ_MODE_VALUES } from "../../../src/settingsEnums";
import { NodeRig, type In, type Sent } from "./_rig";

const serialOut = (mode: string, sampleRate = 44100) => new NodeRig(lsdjSerialOutNode, { mode }, sampleRate);
/** The host MIDI sent, each as [frame, ...bytes]. */
const midiOut = (sent: Sent[]): number[][] => sent.filter((s) => s.port === "midiOut").map((s) => [s.frame, ...s.data]);
const data = (sent: Sent[]): number[][] => sent.filter((s) => s.port === "midiOut").map((s) => s.data);
/** A flag-framed MI.OUT byte (0x80 | the protocol byte), captured at `frame`. */
const framed = (cmd: number, frame: number): In => ({ frame, data: [0x80 | (cmd & 0x7f)] });

test("its ports: the Game Boy's serial out in, host MIDI out, and the lsdj-sync role's settings, as rp.lsdj.sync takes them", () => {
  expect(serialOut("midiOut").ports()).toEqual(["in bytes serialOut", "out events midiOut"]);
  expect(lsdjSerialOutNode.descriptor().id).toBe(LSDJ_SERIAL_OUT_NODE_TYPE);
  const caps = lsdjSerialOutNode.layout(lsdjSerialOutNode.parseSettings({})).ports.slice(1).map((p) => p.capacity);
  expect(caps).toEqual([{ messages: 1024, bytes: 8192 }]);
  expect(lsdjSerialOutNode.reshapes).toBe(true);
  // One schema: the same defaults, the same refusals, the same JSON Schema in the descriptor.
  expect(lsdjSerialOutNode.parseSettings({})).toEqual({ mode: "midiSync", tempoDivisor: 1, autoStart: false });
  expect(() => lsdjSerialOutNode.parseSettings({ mode: "sync" })).toThrow("mode: Invalid option");
  expect(() => lsdjSerialOutNode.parseSettings({ tempoDivisor: 9 })).toThrow("tempoDivisor: Too big");
  expect(JSON.stringify(lsdjSerialOutNode.descriptor().settings)).toBe(JSON.stringify(lsdjSyncNode.descriptor().settings));
});

// --- MI.OUT (mode 7) ------------------------------------------------------------------------------------------------

test("MI.OUT: the serial-out bytes decode to host MIDI in their own block, each message at its completing byte's frame", () => {
  // A byte every eight frames, as SameBoy clocks MI.OUT's port (one bit an audio frame); each flag-frames one protocol
  // byte: START, clock, and a NoteOn ch0 note 0x69, whose command and value are two bytes and which completes at the
  // value's.
  const sent = serialOut("midiOut").run({ frames: 1024, inputs: { serialOut: [framed(0x7d, 7), framed(0x7f, 15), framed(0x70, 23), framed(0x69, 31)] } });
  expect(midiOut(sent)).toEqual([[7, 0xfa], [15, 0xf8], [31, 0x90, 0x69, 0x7f]]);
  expect(sent.every((s) => s.port === "midiOut" && s.queued === s.frame)).toBe(true);
});

test("MI.OUT: a wire frame split across two blocks' bytes decodes once, at its completing byte's frame in the later block", () => {
  const rig = serialOut("midiOut");
  expect(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 1000, data: [0x7f] }] } })).toEqual([]);
  expect(midiOut(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 3, data: [0x80] }] } }))).toEqual([[3, 0xf8]]);
});

// --- master sync (mode 8) -------------------------------------------------------------------------------------------

test("master sync: each byte a clock at its frame, the first opening the run with LSDj's song row and 0xFA there", () => {
  const sent = serialOut("masterSync").run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [5] }, { frame: 900, data: [6] }] } });
  expect(midiOut(sent)).toEqual([[0, 0x90, 5, 0x7f], [0, 0xfa], [0, 0xf8], [900, 0xf8]]);
});

/** The block and frame of the first 0xFC after a run's one byte at `frame` of block 0, in 1024-frame blocks at
 *  `sampleRate`, every block's or each block's by its index. */
function stopOf(frame: number, sampleRate: number | ((block: number) => number)): [number, number] | null {
  const rateOf = (b: number) => (typeof sampleRate === "number" ? sampleRate : sampleRate(b));
  const rig = serialOut("masterSync", rateOf(0));
  for (let b = 0; b < 16; b++) {
    const inputs: Record<string, In[]> = b === 0 ? { serialOut: [{ frame, data: [5] }] } : {};
    const sent = rig.run({ frames: 1024, sampleRate: rateOf(b), playing: true, inputs });
    const stop = sent.find((s) => s.data[0] === 0xfc);
    if (stop) return [b, stop.frame];
  }
  return null;
}

test("master sync stops a run at its mark, the first frame its silence passes 200 ms, in the block that holds it", () => {
  // 200 ms is 8820 frames at 44.1 kHz, so a byte at frame f stops at f + 8821.
  expect(stopOf(0, 44100)).toEqual([8, 8821 - 8 * 1024]);
  expect(stopOf(1000, 44100)).toEqual([9, 9821 - 9 * 1024]);
});

test("master sync's 200 ms stop is 200 ms at the host's rate: 9600 frames at 48 kHz, not 8820", () => {
  expect(stopOf(0, 48000)).toEqual([9, 9601 - 9 * 1024]);
  expect(stopOf(1000, 48000)).toEqual([10, 10601 - 10 * 1024]);
});

test("master sync's stop over a change in the host's rate is 200 ms after the run's byte, at a frame of its block", () => {
  // Each block's rate is its transport's. A byte at frame 100 at 48 kHz, then 44.1 kHz from block 9, whose start is
  // 9116 frames on at 48 kHz, 8375.3 at 44.1 kHz: the silence passes 8820 at its frame 445. And 44.1 kHz, then 48 kHz
  // from block 8, 8092 frames on, 8807.6 at 48 kHz: it passes 9600 at its frame 793.
  expect(stopOf(100, (b) => (b < 9 ? 48000 : 44100))).toEqual([9, 445]);
  expect(stopOf(100, (b) => (b < 8 ? 44100 : 48000))).toEqual([8, 793]);
});

// --- state across settings changes ----------------------------------------------------------------------------------

test("leaving master sync drops its state, so coming back opens a fresh run", () => {
  const rig = serialOut("masterSync");
  const block = (bytes: In[]) => data(rig.run({ frames: 1024, inputs: { serialOut: bytes } }));
  expect(block([{ frame: 100, data: [5] }])).toEqual([[0x90, 5, 0x7f], [0xfa], [0xf8]]); // a run opens
  rig.set({ mode: "off" });
  block([]);
  rig.set({ mode: "masterSync" });
  // Kept, the stale open run would swallow this run's NoteOn and 0xFA.
  expect(block([{ frame: 0, data: [7] }])).toEqual([[0x90, 7, 0x7f], [0xfa], [0xf8]]);
  expect(rig.creates).toBe(1);
});

test("MI.OUT keeps its decoder's state across a mode change: a wire frame begun before it completes after it", () => {
  // The first half of a framed clock, then a block in master sync (whose run does not touch MI.OUT's bits) and one in
  // off, then MI.OUT again: the second half completes the clock, once.
  const rig = serialOut("midiOut");
  expect(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 0, data: [0x7f] }] } })).toEqual([]);
  rig.set({ mode: "masterSync" });
  expect(data(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 10, data: [3] }] } }))).toEqual([[0x90, 3, 0x7f], [0xfa], [0xf8]]);
  rig.set({ mode: "off" });
  expect(rig.run({ frames: 1024 })).toEqual([]);
  rig.set({ mode: "midiOut" });
  expect(midiOut(rig.run({ frames: 1024, inputs: { serialOut: [{ frame: 5, data: [0x80] }] } }))).toEqual([[5, 0xf8]]);
  expect(rig.creates).toBe(1);
});

test("every other mode sends nothing and reads nothing of its input", () => {
  // Another mode's input is usually empty, but not for a cart the MI.MAP controller forces (to midiMap, or off;
  // lsdjSerialOut.ts says why), so each mode is handed one, which throws when read: the node never reads it.
  for (const mode of LSDJ_MODE_VALUES.filter((m) => m !== "midiOut" && m !== "masterSync")) {
    const rig = serialOut(mode);
    const process = rig.node.process.bind(rig.node);
    rig.node.process = (b) => {
      const messages = b.messages;
      b.messages = (port) => {
        if (port === "serialOut") throw new Error(`${mode} read its serialOut`);
        return messages.call(b, port);
      };
      return process(b);
    };
    expect(rig.run({ frames: 1024, playing: true, inputs: { serialOut: [framed(0x7f, 7), { frame: 100, data: [5] }] } }), mode).toEqual([]);
  }
});

test("MI.OUT and master sync send each message from an array the node made once, one a message length", () => {
  // The rig keeps what send was handed: in every block the node passes, for each length a message has, the array it
  // passed first. That is the reuse, which the rig can see; that a warm block allocates nothing at all is
  // test-native/dsp-bench.test.ts's to show, in a profile build (tools/run-profile.sh stats).
  // MI.OUT: a clock (1 byte), a NoteOn (3) and a program change (2), the same each block.
  const miOut = [framed(0x7f, 7), framed(0x70, 15), framed(60, 23), framed(0x78, 31), framed(5, 39)];
  // Master sync: a run's first block sends its NoteOn (3), 0xFA and a clock (1), and each block after a clock.
  const master = (b: number): In[] => [{ frame: 100, data: [b === 0 ? 4 : 9] }];
  for (const [mode, bytes] of [["midiOut", () => miOut], ["masterSync", master]] as const) {
    const rig = serialOut(mode);
    const byLength = new Map<number, ArrayLike<number>>();
    for (let b = 0; b < 4; b++) {
      const sent = rig.run({ frames: 1024, inputs: { serialOut: bytes(b) } });
      expect(sent.length > 0).toBeTruthy();
      sent.forEach((s, i) => {
        const source = rig.sources[i];
        expect(s.port).toBe("midiOut");
        expect(source.length).toBe(s.data.length);
        if (!byLength.has(source.length)) byLength.set(source.length, source);
        expect(byLength.get(source.length) === source).toBeTruthy();
      });
    }
    expect([...byLength.keys()].sort()).toEqual(mode === "midiOut" ? [1, 2, 3] : [1, 3]);
  }
});
