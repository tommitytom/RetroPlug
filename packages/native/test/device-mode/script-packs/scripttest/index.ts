// The scripttest pack: device script nodes made to hold the MicroQuickJS host to its contract
// (graph-runtime's script/MqjsHost.hpp), which ScriptPack.test.cpp runs on both builds of the host. Built by the
// device-pack toolchain as a test fixture (packages/native/cmake/tests.cmake), never by a product.
//
//   rp.scripttest.probe     every part of the block API: what it reads (frames, transport, a control input and a param,
//                           its events and bytes inputs) it sends on, and it calls what a block refuses
//   rp.scripttest.settings  its settings as create was given them, sent back as text, a byte a character
//   rp.scripttest.hooks     what portsChanged and activate were told, sent each block
//   rp.scripttest.steady    the whole block API with nothing made a block: what a warmed block is held to
//   rp.scripttest.keeper    keeps a message, a stream and a change of its block in the pack's globals
//   rp.scripttest.reader    reads what the keeper kept, as another node, and sends what each read did
//   rp.scripttest.busy      a fixed count of work a block, long enough for the engine to poll its interrupt handler
//   rp.scripttest.throws, .spins, .thenable, .fault, .no-process, .create-throws: one fault each
import { bytes, control, defineNode, events, param } from "../../../../../retroplug/src/graph/defineNode";
import { s } from "../../../../../retroplug/src/graph/settingsSchema";
import { registerScriptNodes } from "../../../../../retroplug/src/script/register";

const byte = (v: number): number => v & 0xff;

export const probeNode = defineNode({
  id: "rp.scripttest.probe",
  ports: {
    ctl: control.in(),
    ev: events.in(),
    raw: bytes.in(),
    out: events.out({ messages: 64, bytes: 1024 }),
    bout: bytes.out({ messages: 64, bytes: 1024 }),
    level: control.out(),
  },
  params: { gain: param.float({ name: "Gain", min: 0, max: 1, default: 0.5 }) },
  create: () => {
    const summary = [0, 0, 0, 0, 0, 0, 0, 0];
    return {
      process(b) {
        const t = b.transport;
        summary[0] = byte(b.frames);
        summary[1] = byte(b.frames >> 8);
        summary[2] = t.playing ? 1 : 0;
        summary[3] = byte(Math.round(t.tempo));
        summary[4] = byte(Math.floor(t.ppq * 4));
        summary[5] = byte(Math.round(t.sampleRate / 1000));
        summary[6] = byte(Math.round(b.control("gain").start * 100));
        summary[7] = b.control("gain").changes.length;
        b.send("out", 0, summary);
        // The control input, doubled, at the frames it changed.
        const ctl = b.control("ctl");
        b.emit("level", 0, ctl.start * 2);
        for (let i = 0; i < ctl.changes.length; i++) b.emit("level", ctl.changes[i].frame, ctl.changes[i].value * 2);
        // Every message of each input on, at its frame, with its flags.
        const ev = b.messages("ev");
        for (let i = 0; i < ev.length; i++) b.send("out", ev[i].frame, ev[i].data, ev[i].flags);
        const raw = b.messages("raw");
        for (let i = 0; i < raw.length; i++) b.send("bout", raw[i].frame, raw[i].data, raw[i].flags);
        // What a block refuses: a port it does not have, an input named as an output, an audio port. Each a
        // TypeError, which the node sends as a code. (Each catch binds a name of its own: MicroQuickJS refuses two
        // catch clauses of one function that bind the same name.)
        const codes = [0xe0];
        try {
          b.messages("nope");
        } catch (e1) {
          codes.push(e1 instanceof TypeError ? 1 : 0x7f);
        }
        try {
          b.send("ev", 0, [1]);
        } catch (e2) {
          codes.push(e2 instanceof TypeError ? 2 : 0x7f);
        }
        try {
          b.audio("ev");
        } catch (e3) {
          codes.push(e3 instanceof TypeError ? 3 : 0x7f);
        }
        b.send("out", b.frames - 1, codes);
      },
    };
  },
});

export const settingsNode = defineNode({
  id: "rp.scripttest.settings",
  settings: s.object({
    count: s.int({ min: -1000, max: 100000, default: 7 }),
    ratio: s.number(0.25),
    on: s.bool(false),
    name: s.string("abc"),
    mode: s.enum(["slow", "fast"], "slow"),
    taps: s.array(s.int({ min: 0, max: 255, default: 0 }), [1, 2, 3]),
    inner: s.object({ depth: s.int({ min: 0, max: 9, default: 4 }) }),
    // Any JSON at all: what holds a field name or a nesting a device's decode refuses.
    extra: s.json<unknown>({}),
  }),
  ports: { out: events.out({ messages: 4, bytes: 512 }) },
  create: ({ settings }) => {
    const text = JSON.stringify(settings);
    const data = new Uint8Array(text.length);
    for (let i = 0; i < text.length; i++) data[i] = text.charCodeAt(i);
    return {
      process(b) {
        b.send("out", 0, data);
      },
    };
  },
});

export const hooksNode = defineNode({
  id: "rp.scripttest.hooks",
  ports: { out: events.out({ messages: 4, bytes: 64 }), spare: events.out() },
  create: ({ sampleRate, maxFrames }) => {
    const seen = [0, 0, 0, 0, byte(Math.round(sampleRate / 1000)), byte(maxFrames >> 4)];
    return {
      portsChanged(connected) {
        seen[0]++;
        seen[1] = connected.length;
        seen[2] = (connected[0] ? 1 : 0) | (connected[1] ? 2 : 0);
      },
      activate(rate, frames) {
        seen[3]++;
        seen[4] = byte(Math.round(rate / 1000));
        seen[5] = byte(frames >> 4);
      },
      process(b) {
        b.send("out", 0, seen);
      },
    };
  },
});

// Every read of the API a MIDI-rate node makes, each block, and an emit and a send of what it read, with nothing made:
// numbers it reads are whole (frames, flags, bytes, the stream's values as the test sets them), arrays refilled.
export const steadyNode = defineNode({
  id: "rp.scripttest.steady",
  ports: { ctl: control.in(), ev: events.in(), out: events.out({ messages: 64, bytes: 1024 }), level: control.out() },
  params: { gain: param.int({ name: "Gain", min: 0, max: 100, default: 50 }) },
  create: () => {
    const summary = [0, 0, 0, 0];
    return {
      process(b) {
        const t = b.transport;
        summary[0] = b.frames & 0xff;
        summary[1] = t.playing ? 1 : 0;
        summary[2] = t.tempo & 0xff;
        summary[3] = b.control("gain").start & 0xff;
        b.send("out", 0, summary);
        const ctl = b.control("ctl");
        let total = ctl.start;
        const changes = ctl.changes;
        for (let i = 0; i < changes.length; i++) total += changes[i].value + changes[i].frame;
        b.emit("level", 0, total);
        const ev = b.messages("ev");
        for (let i = 0; i < ev.length; i++) b.send("out", ev[i].frame, ev[i].data, ev[i].flags);
      },
    };
  },
});

// What a pack must not do, and the host must survive: objects of a block kept past it, in the pack's globals, read
// later by another node, which may hold the keeper's slot by then.
let keptMessage: { frame: number; data: Uint8Array; flags: number } | null = null;
let keptStream: { start: number; changes: readonly { frame: number; value: number }[] } | null = null;
let keptChange: { frame: number; value: number } | null = null;

export const keeperNode = defineNode({
  id: "rp.scripttest.keeper",
  ports: { a: control.in(), b: control.in(), ev: events.in(), out: events.out() },
  create: () => ({
    process(blk) {
      const ev = blk.messages("ev");
      if (ev.length > 0) keptMessage = ev[ev.length - 1];
      keptStream = blk.control("b");
      if (keptStream.changes.length > 0) keptChange = keptStream.changes[0];
    },
  }),
});

// Sends, a byte a read, what reading each kept object did: 1 it read a value, 2 it threw the host's TypeError for an
// object kept past its block, 4 another TypeError, 3 anything else.
export const readerNode = defineNode({
  id: "rp.scripttest.reader",
  ports: { out: events.out() },
  create: () => ({
    process(b) {
      const codes = [0, 0, 0, 0, 0];
      const reads = [
        () => keptMessage && keptMessage.frame,
        () => keptMessage && keptMessage.data,
        () => keptStream && keptStream.start,
        () => keptStream && keptStream.changes,
        () => keptChange && keptChange.value,
      ];
      for (let i = 0; i < reads.length; i++) {
        try {
          reads[i]();
          codes[i] = 1;
        } catch (e) {
          const kept = e instanceof TypeError && String(e.message).indexOf("kept past its block") >= 0;
          codes[i] = kept ? 2 : e instanceof TypeError ? 4 : 3;
        }
      }
      b.send("out", 0, codes);
    },
  }),
});

const quiet = { out: events.out() };

export const throwsNode = defineNode({
  id: "rp.scripttest.throws",
  ports: quiet,
  create: () => ({
    process() {
      throw new RangeError("the throws node threw");
    },
  }),
});

export const busyNode = defineNode({
  id: "rp.scripttest.busy",
  ports: quiet,
  create: () => ({
    process(b) {
      let n = 0;
      for (let i = 0; i < 200000; i++) n += i & 1;
      b.send("out", 0, [n & 0x7f]);
    },
  }),
});

export const spinsNode = defineNode({
  id: "rp.scripttest.spins",
  ports: quiet,
  create: () => ({
    process() {
      let n = 0;
      for (;;) n++;
    },
  }),
});

export const thenableNode = defineNode({
  id: "rp.scripttest.thenable",
  ports: quiet,
  create: () => ({
    process() {
      return { then() {} } as unknown as void;
    },
  }),
});

export const faultNode = defineNode({
  id: "rp.scripttest.fault",
  ports: quiet,
  create: () => ({
    process() {
      return "fault" as const;
    },
  }),
});

export const noProcessNode = defineNode({
  id: "rp.scripttest.no-process",
  ports: quiet,
  create: () => ({}) as unknown as { process(): void },
});

export const createThrowsNode = defineNode({
  id: "rp.scripttest.create-throws",
  ports: quiet,
  create: () => {
    throw new TypeError("create would not");
  },
});

registerScriptNodes([probeNode, settingsNode, hooksNode, steadyNode, keeperNode, readerNode, busyNode, throwsNode,
                     spinsNode, thenableNode, faultNode, noProcessNode, createThrowsNode]);
