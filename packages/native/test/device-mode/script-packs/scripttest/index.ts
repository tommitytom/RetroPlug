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
//   rp.scripttest.throws-later  throws at its second block, with words longer than a fault keeps
//   rp.scripttest.sends     sends data that is no Uint8Array, whose reads allocate
//   rp.scripttest.skipper   skipped when idle (idle: "skip"): counts its process calls, and forwards what it gets
//   rp.scripttest.replace-throws, .has-own-throws, .sort-throws, .repeat-throws: a throw where MicroQuickJS would
//                           drop it (a String.replace function's, a hasOwnProperty key's toString's, a sort
//                           comparator's, a String.repeat count's valueOf's), which must fault the node as any throw
//                           does
//   rp.scripttest.sort-spins  a sort comparator that never returns, where MicroQuickJS would drop the deadline's
//                           interrupt: it spins when its settings say `on`, so the toolchain's run of every node on
//                           its defaults passes it
//   rp.scripttest.spins-in  spins as .spins does, behind an events and a bytes input: a block's deadline grows with
//                           the messages on both
//   rp.scripttest.deep      reads and forwards its messages from the deepest call into JavaScript the host allows on
//                           the C stack, and says how deep that was
import { type Block, bytes, control, defineNode, events, param } from "../../../../../retroplug/src/graph/defineNode";
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

// Sends what is no Uint8Array, every block: an array-like object, an Array of objects with a valueOf, a Float64Array
// of non-integers (each boxed at 32 bits) and, through forward, an object whose data is an array-like. Reading each
// allocates (a property by name, a valueOf's call, a boxed float), so a collection can run between the host's reads of
// the data, and move it.
export const sendsNode = defineNode({
  id: "rp.scripttest.sends",
  ports: quiet,
  create: () => ({
    process(b) {
      b.send("out", 0, { length: 3, 0: 0x90, 1: 60, 2: 100 } as unknown as ArrayLike<number>);
      const v = (n: number) => ({ valueOf: () => n }) as unknown as number;
      b.send("out", 1, [v(0x80), v(61), v(0)]);
      b.send("out", 2, new Float64Array([144.5, 62.25, 100.75]));
      b.forward("out", { frame: 3, data: { length: 2, 0: 0xc0, 1: 5 } as unknown as ArrayLike<number>, flags: 1 });
    },
  }),
});

// Runs its first block, then throws with words longer than a fault keeps (rp_script.h's rp.script.fault/1: 159 bytes),
// so an export's trial load passes it and a box's console shows its words cut short.
export const throwsLaterNode = defineNode({
  id: "rp.scripttest.throws-later",
  ports: quiet,
  create: () => {
    let calls = 0;
    return {
      process() {
        if (++calls < 2) return;
        throw new RangeError("the throws-later node threw on its second block, with words that run on past what a " +
          "fault keeps of them and past what a line of the box's console holds, which cuts them short there");
      },
    };
  },
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

// Skipped when idle, so it sends on `calls` each call how many process calls it has had and its level input's start (in
// hundredths): a block that brings it nothing (after its first) sends nothing there, as it is not called. Each message
// goes on through forward, twice: on `out` as the block handed it out, a note-on's velocity written to 1 in its data
// first (forward sends the data as it holds it then), and on `made` as an object of the node's own, a byte long, a
// frame later and flagged.
export const skipperNode = defineNode({
  id: "rp.scripttest.skipper",
  idle: "skip",
  ports: { in: events.in(), level: control.in(), out: events.out(), made: events.out(), calls: events.out() },
  params: { p: param.float({ name: "P", min: 0, max: 1, default: 0 }) },
  create: () => {
    let calls = 0;
    return {
      process(b) {
        calls++;
        b.send("calls", 0, [calls, Math.round(b.control("level").start * 100)]);
        const messages = b.messages("in");
        for (let i = 0; i < messages.length; i++) {
          const m = messages[i];
          if (m.data.length === 3 && (m.data[0] & 0xf0) === 0x90) m.data[2] = 1;
          b.forward("out", m);
          b.forward("made", { frame: m.frame + 1, data: [m.data.length ? m.data[0] : 0], flags: 1 });
        }
      },
    };
  },
});

export const replaceThrowsNode = defineNode({
  id: "rp.scripttest.replace-throws",
  ports: quiet,
  create: () => ({
    process(b) {
      const r = "abc".replace("b", () => {
        throw new RangeError("the replace function threw");
      });
      b.send("out", 0, [r.length]);
    },
  }),
});

export const hasOwnThrowsNode = defineNode({
  id: "rp.scripttest.has-own-throws",
  ports: quiet,
  create: () => ({
    process(b) {
      const key = {
        toString(): string {
          throw new RangeError("the key's toString threw");
        },
      };
      const has = { a: 1 }.hasOwnProperty(key as unknown as PropertyKey);
      b.send("out", 0, [has ? 1 : 0]);
    },
  }),
});

export const sortThrowsNode = defineNode({
  id: "rp.scripttest.sort-throws",
  ports: quiet,
  create: () => ({
    process(b) {
      const sorted = [3, 2, 1].sort(() => {
        throw new RangeError("the sort comparator threw");
      });
      b.send("out", 0, sorted);
    },
  }),
});

export const repeatThrowsNode = defineNode({
  id: "rp.scripttest.repeat-throws",
  ports: quiet,
  create: () => ({
    process() {
      const count = {
        valueOf(): number {
          throw new RangeError("the repeat count's valueOf threw");
        },
      };
      "x".repeat(count as unknown as number);
    },
  }),
});

// The switch a node that spins by design waits for, off by default: the toolchain runs every node on its defaults.
const spinSwitch = s.object({ on: s.bool(false) });

export const sortSpinsNode = defineNode({
  id: "rp.scripttest.sort-spins",
  settings: spinSwitch,
  ports: quiet,
  create: ({ settings }) => ({
    process(b) {
      if (!settings.on) return;
      const sorted = [3, 2, 1].sort((x, y) => {
        for (;;) if (x === y) return 0;
      });
      b.send("out", 0, sorted);
    },
  }),
});

export const spinsInNode = defineNode({
  id: "rp.scripttest.spins-in",
  ports: { in: events.in(), raw: bytes.in(), out: events.out() },
  create: () => ({
    process() {
      let n = 0;
      for (;;) n++;
    },
  }),
});

// Calls forEach from inside forEach's callback until the engine refuses the next call into JavaScript (the host's
// limit on a context's C re-entries), reads its block's messages and forwards them from the deepest callback (so the
// host's refill, a SysEx's array among it, runs there), then sends how deep that was on `depth`. A throw from below the
// deepest call, or from the work done there, goes on up and faults the node.
export const deepNode = defineNode({
  id: "rp.scripttest.deep",
  ports: { in: events.in(), out: events.out(), depth: events.out() },
  create: () => {
    const one = [0];
    let block: Block | null = null;
    let depth = 0;
    let reached = 0;
    let done = false;
    const down = (): void => {
      const at = ++depth;
      if (at > reached) reached = at;
      try {
        one.forEach(down);
      } catch (e) {
        if (done || reached !== at) throw e;
      }
      if (!done && reached === at) {
        done = true;
        const b = block as Block;
        const messages = b.messages("in");
        for (let i = 0; i < messages.length; i++) b.forward("out", messages[i]);
      }
      depth--;
    };
    return {
      process(b) {
        block = b;
        depth = 0;
        reached = 0;
        done = false;
        down();
        block = null;
        b.send("depth", 0, [reached]);
      },
    };
  },
});

registerScriptNodes([probeNode, settingsNode, hooksNode, steadyNode, keeperNode, readerNode, busyNode, throwsNode,
                     spinsNode, thenableNode, faultNode, noProcessNode, createThrowsNode, skipperNode,
                     throwsLaterNode, sendsNode, replaceThrowsNode, hasOwnThrowsNode, spinsInNode, deepNode,
                     sortThrowsNode, sortSpinsNode, repeatThrowsNode]);
