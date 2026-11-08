// The Launchpad core, end to end, through the PORTABLE CORE only - run by
// packages/retroplug/scripts/build-device-pack.mjs --scenario under Node and under MicroQuickJS, whose two
// transcripts must match byte for byte.
//
// It plays what a host does: take the device, feed it pad presses and a 24 PPQN clock block by block, write out
// what comes back, give the device back - plus a port scan against a fake rig. Every line printed is something a
// real host would put on a wire (LED bytes to the device, launch bytes to the cart) or a scan result it would act
// on, so a difference between the engines is a difference a player would see.
//
// It also has to REACH the core, not just import it: MicroQuickJS parses the whole bundle, so a syntax problem
// anywhere fails the run, but a missing library call or an array written past its end only fails on the path that
// executes it. So past the end-to-end session below, the later sections deliberately walk the branches a session
// does not: every LED form, the decoder's other message kinds, the profiles and their model confirmation, and a
// scan's follow-up / cancel / failure / quiet paths.

import {
  ControllerSession, PRO_MK3, Palette, PortScan, Surface, decodeMessages, deviceInquiry, deviceName, familyHex,
  isModelReply, modelQuery, parseInquiryReply, palette, profileForFamily, profilesForFamily, rgb,
  type MidiPortApi, type PlaybackPosition, type PlaybackGrid,
} from "../portable";

const out = (line: string): void => console.log(line);
const emit = (what: string, data: unknown): void => out(what + " " + JSON.stringify(data));

// --- a session --------------------------------------------------------------------------------------------------

/** A dependency-free playback model: four channels that advance one row per 96 ticks, wrapping at six rows. Rows
 *  0..3 have content on pu1/pu2, 0..1 on wav, and noi is empty. Enough to see a playhead move and channels diverge. */
class FakeModel {
  readonly channelCount = 4;
  private readonly rows = [0, 0, 0, 0];
  private readonly remaining = [96, 96, 96, 96];
  private readonly playing = [false, false, false, false];
  private advanced = 0;
  private readonly pos: PlaybackPosition = { playing: false, channels: [] };
  private readonly gridView: PlaybackGrid & { revision: number };

  constructor() {
    for (let i = 0; i < 4; i++) this.pos.channels.push({ playing: false, songRow: null });
    const rows = [4, 4, 2, 0];
    this.gridView = {
      rowCount: 6,
      channelCount: 4,
      revision: 0,
      hasContent: (ch: number, row: number) => row >= 0 && row < 6 && row < rows[ch],
    };
  }

  position(): PlaybackPosition {
    return this.pos;
  }

  grid(): PlaybackGrid {
    return this.gridView;
  }

  launch(row: number): void {
    for (let i = 0; i < 4; i++) {
      this.rows[i] = row;
      this.remaining[i] = 96;
      this.playing[i] = true;
    }
    this.sync();
  }

  advance(ticks: number): void {
    this.advanced += ticks;
    for (let i = 0; i < 4; i++) {
      if (!this.playing[i]) continue;
      this.remaining[i] -= ticks;
      while (this.remaining[i] <= 0) {
        this.rows[i] = (this.rows[i] + 1) % 6;
        this.remaining[i] += 96;
      }
    }
    this.sync();
  }

  stop(): void {
    for (let i = 0; i < 4; i++) this.playing[i] = false;
    this.sync();
  }

  reset(): void {
    for (let i = 0; i < 4; i++) {
      this.rows[i] = 0;
      this.remaining[i] = 96;
      this.playing[i] = false;
    }
    this.sync();
  }

  advancedTicks(): number {
    return this.advanced;
  }

  private sync(): void {
    let any = false;
    for (let i = 0; i < 4; i++) {
      const p = this.pos.channels[i];
      p.playing = this.playing[i];
      p.songRow = this.playing[i] ? this.rows[i] : null;
      any = any || p.playing;
    }
    this.pos.playing = any;
  }
}

const launches: number[][] = [];
const model = new FakeModel();
const session = new ControllerSession((c) => {
  // Paint a row of content, the playhead bright, and launch on a press - the shape every controller app has.
  for (let y = 0; y < 4; y++) {
    c.surface.setPad(0, y, { mode: "static", colour: palette(Palette.greenDim) });
  }
  const row = c.playback.position().channels[0].songRow ?? 0;
  c.surface.setPad(1, row, { mode: "static", colour: palette(Palette.greenBright) });
  if (c.events.length > 0) c.target.launch(1);
}, {
  playback: model,
  target: { maxRow: 253, launch: (row) => launches.push([0x90, row, 100]), release: (row) => launches.push([0x80, row, 0]) },
  config: {},
});

const pad = (x: number, y: number, velocity: number): number[] => [0x90, (7 - y) * 10 + x + 11, velocity];
const cc = (n: number, value: number): number[] => [0xb0, n, value];

emit("connect", session.connect());

interface Block {
  input: number[][];
  tick: number;
  transport: boolean;
  relocate: boolean;
  /** The link reports a NEW device before this block, as a pair swapped inside one UI frame does: the host takes it
   *  again, which invalidates the surface so the block repaints everything. */
  reconnect?: boolean;
}
const blocks: Block[] = [
  { input: [], tick: 0, transport: false, relocate: false },
  { input: [pad(0, 2, 127), pad(0, 2, 0)], tick: 0, transport: false, relocate: false },
];
for (let tick = 0; tick <= 480; tick += 12) {
  const input: number[][] = [];
  if (tick === 108) input.push(pad(1, 4, 100));
  if (tick === 300) input.push(cc(PRO_MK3.buttons.up, 127), cc(PRO_MK3.buttons.up, 0));
  if (tick === 312) input.push(cc(PRO_MK3.buttons.down, 127));
  if (tick === 396) input.push(pad(6, 1, 90));
  blocks.push({ input, tick, transport: true, relocate: false });
}
// A generation change mid-run: the link reports a NEW device, so the host takes it again - Programmer mode and a full
// repaint, with the predictor left where the cart is.
blocks.push({ input: [pad(2, 1, 100)], tick: 492, transport: true, relocate: false, reconnect: true });
// A seek: the tick jumps far ahead. The session must not advance the model through it, so `ticks` is 0.
blocks.push({ input: [], tick: 4000, transport: true, relocate: true });
blocks.push({ input: [], tick: 480, transport: false, relocate: false });

for (let i = 0; i < blocks.length; i++) {
  launches.length = 0;
  if (blocks[i].reconnect) emit("reconnect " + i, session.connect());
  const leds = session.update(blocks[i]);
  if (leds.length > 0 || launches.length > 0) emit("block " + i, { leds, launches: launches.slice() });
}
emit("ticks", model.advancedTicks());
emit("disconnect", session.disconnect());

// --- a scan -----------------------------------------------------------------------------------------------------

// Two outputs, one busy; the device answers on a different interface's input than the output it was asked
// through, with a keyboard chattering clock and notes into every window. The follow-up asks the model question
// on the pair that answered, and only the model's own reply confirms it.
const PRO_MK3_REPLY = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0, 0, 0, 1, 2, 3, 0xf7];
const MODEL_REPLY = [0xf0, 0x00, 0x20, 0x29, 0x02, 0x0e, 0x00, 0x00, 0x00, 0xf7];
const queues: { [handle: string]: number[][] } = {};
const names: { [handle: string]: string } = {};
let nextHandle = 1;
const deliver = (input: string, bytes: number[]): void => {
  for (const h of Object.keys(queues)) if (names[h] === input) queues[h].push(bytes);
};
const openPort = (input: boolean, name: string): number => {
  if (name === "Busy out" || name === "Busy in") return 0;
  const h = String(nextHandle++);
  names[h] = name;
  if (input) queues[h] = [];
  return Number(h);
};
const api: MidiPortApi = {
  openInputs: (list) => list.map((n) => openPort(true, n)),
  openOutput: (name) => openPort(false, name),
  send(handle, bytes) {
    const name = names[String(handle)];
    deliver("Keys", [0xf8]);
    deliver("Keys", [0x90, 60, 64]);
    if (name === "IF B out" && bytes[1] === 0x7e) deliver("IF A in", PRO_MK3_REPLY);
    if (name === "IF B out" && isModelReply(PRO_MK3, bytes)) deliver("IF A in", bytes.slice()); // an echo: not an answer
    if (name === "IF B out" && bytes[1] === 0x00 && bytes[2] === 0x20) deliver("IF A in", MODEL_REPLY);
    return true;
  },
  read(handle) {
    const q = queues[String(handle)] ?? [];
    queues[String(handle)] = [];
    return q;
  },
  close(handle) {
    delete queues[String(handle)];
    delete names[String(handle)];
  },
};

const scan = new PortScan(api, ["Keys", "Busy in", "IF A in"], ["Busy out", "Keys", "IF B out"], deviceInquiry(), 300, {
  followUp: (reply) => {
    const parsed = parseInquiryReply(reply.bytes);
    return parsed && profileForFamily(parsed.family) ? [modelQuery(PRO_MK3)] : [];
  },
});
for (let t = 0; scan.step(t); t += 16) { /* a frame at a time */ }
const status = scan.status();
emit("scan", status);
for (const r of status.replies) {
  const parsed = parseInquiryReply(r.bytes);
  if (parsed) emit("found", { input: r.input, output: r.output, name: deviceName(parsed.family) });
  emit("model reply", [r.bytes, isModelReply(PRO_MK3, r.bytes)]);
}
emit("open handles", Object.keys(names).length);

// --- the branches a session does not walk -----------------------------------------------------------------------

// Every LED form through the surface: static/pulse/flash in palette and RGB (RGB forces the bulk SysEx), a refused
// one (pulse carries a palette index only), edge buttons by name, and the indices that are not there.
const surface = new Surface();
surface.setPad(0, 0, { mode: "static", colour: palette(Palette.red) });
surface.setPad(1, 0, { mode: "pulse", colour: palette(Palette.yellow) });
surface.setPad(2, 0, { mode: "flash", colour: palette(Palette.greenBright), alt: palette(Palette.greenDim) });
surface.setPad(3, 0, { mode: "static", colour: rgb(127, 40, 200) });
surface.setPad(4, 0, { mode: "flash", colour: rgb(1, 2, 3), alt: rgb(300, -5, NaN) }); // clamped, NaN -> 0
surface.setPad(5, 0, { mode: "pulse", colour: rgb(1, 2, 3) }); // not renderable: refused, stays off
surface.setButton("logo", { mode: "static", colour: palette(Palette.red) });
surface.setButton("nonexistent", { mode: "static", colour: palette(Palette.red) });
surface.set(0, { mode: "static", colour: palette(1) });
surface.set(11.5, { mode: "static", colour: palette(1) });
surface.set(200, { mode: "static", colour: palette(1) });
surface.setPad(-1, 9, { mode: "static", colour: palette(1) });
emit("surface bulk", surface.flush());
surface.setPad(0, 0, { mode: "off" });
surface.setPad(1, 1, { mode: "static", colour: palette(Palette.greenDim) });
emit("surface short", surface.flush());
emit("surface steady", surface.flush());
emit("peek", [surface.peek(81), surface.peek(82), surface.peek(0), surface.peek(11.5), surface.peek(-3)]);
surface.invalidate();
emit("surface repaint", surface.flush().dirty);
surface.clear();
emit("surface clear", surface.flush().dirty);

// The decoder's other message kinds: pad down/up, aftertouch, every named edge button, an unknown CC, a short
// message, a stray status byte and a data byte that is not MIDI.
const toDecode: number[][] = [[0x90, 81, 127], [0x80, 81, 0], [0x90, 81, 0], [0xa0, 81, 64], [0xd0, 64, 0], [0xb0, 1, 0]];
for (const name of Object.keys(PRO_MK3.buttons)) toDecode.push([0xb0, PRO_MK3.buttons[name], 127]);
toDecode.push([0xb0, 127, 127], [0x90, 81], [0xc0, 1, 2], [0x90, 200, 5], [0x90, PRO_MK3.buttons.down, 127]);
emit("decode", decodeMessages(PRO_MK3, toDecode));

// Identification: a known family, an unknown Novation, junk, and the model question the family cannot answer.
emit("profiles", [profileForFamily([0x13, 0x01])?.name ?? null, profileForFamily([0x37, 0x01]),
  profilesForFamily([0x13, 0x01]).length, familyHex([0x37, 0x01]), deviceName([0x37, 0x01]),
  deviceName([0x13, 0x01]), parseInquiryReply([0xf0, 0x7e]), parseInquiryReply([0x90, 1, 2]),
  isModelReply(PRO_MK3, MODEL_REPLY), isModelReply(PRO_MK3, modelQuery(PRO_MK3))]);

// A scan's other endings: cancelled mid-window, a port API that throws, and a reply landing in the quiet
// between windows (dropped).
const quiet: { [handle: string]: number[][] } = {};
let quietNext = 1;
const late: MidiPortApi = {
  openInputs: (list) => list.map(() => { const h = quietNext++; quiet[String(h)] = []; return h; }),
  openOutput: () => quietNext++,
  send: () => true,
  read: (h) => { const q = quiet[String(h)] ?? []; quiet[String(h)] = []; return q; },
  close: (h) => { delete quiet[String(h)]; },
};
const lateScan = new PortScan(late, ["In"], ["Out 1", "Out 2"], deviceInquiry(), 300);
lateScan.step(0);
lateScan.step(300); // Out 1's window closes
for (const h of Object.keys(quiet)) quiet[h].push([0xf0, 0x01, 0xf7]); // too late for Out 1
for (let t = 310; lateScan.step(t); t += 10) { /* Out 2 goes out after the quiet */ }
emit("late reply", lateScan.status().replies.length);
const cancelled = new PortScan(late, ["In"], ["Out 1", "Out 2"], deviceInquiry(), 300);
cancelled.step(0);
cancelled.cancel();
emit("cancelled", [cancelled.status().busy, cancelled.status().done, cancelled.step(10)]);
const broken = new PortScan({ ...late, read: () => { throw new Error("gone"); } }, ["In"], ["Out"], deviceInquiry(), 300);
broken.step(0);
emit("broken", [broken.status().error, broken.status().done]);
emit("quiet handles", Object.keys(quiet).length);
