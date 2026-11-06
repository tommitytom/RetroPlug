// The devtest script pack (packages/devtest/package.json's retroplug.script): a device pack of test node types, which
// the device-pack toolchain (packages/retroplug/scripts/build-device-pack.mjs) compiles to MicroQuickJS bytecode
// through cmake/packages.cmake's retroplug_script_pack. Like the package's C++ types, no product that ships carries it;
// the tests build it, at both widths (packages/retroplug/test/graph/device-pack.test.ts).
import { type Block, defineNode, events } from "../../retroplug/src/graph/defineNode";
import { s } from "../../retroplug/src/graph/settingsSchema";
import { registerScriptNodes } from "../../retroplug/src/script/register";

const clamp7 = (v: number): number => (v < 0 ? 0 : v > 127 ? 127 : v);

// A note on or off moved by `semitones` and held to 0..127; every other message passed on as it came. Each keeps its
// frame and flags, and the output's capacity is its input's. A moved note goes out from a three-byte array of the
// node's own, refilled for each, so a warmed block allocates nothing.
export const scriptTransposeNode = defineNode({
  id: "rp.devtest.script-transpose",
  name: "Script transpose (devtest)",
  category: "test",
  settings: s.object({ semitones: s.int({ min: -127, max: 127, default: 0 }) }),
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: ({ settings }) => {
    let semitones = settings.semitones;
    const note = new Uint8Array(3);
    return {
      onSettings(next) {
        semitones = next.semitones;
      },
      process(b) {
        const messages = b.messages("in");
        for (let i = 0; i < messages.length; i++) {
          const m = messages[i];
          const kind = m.data[0] & 0xf0;
          if ((kind === 0x80 || kind === 0x90) && m.data.length === 3) {
            note[0] = m.data[0];
            note[1] = clamp7(m.data[1] + semitones);
            note[2] = m.data[2];
            b.send("out", m.frame, note, m.flags);
          } else {
            b.send("out", m.frame, m.data, m.flags);
          }
        }
      },
    };
  },
});

// How deep a node's calls into JavaScript may go on the C stack: the host's cap on a node's context
// (graph-runtime's script/rp_mqjs.h, RP_MQJS_MAX_REENTRIES), the host's own call into process the first of them.
const kReentries = 4;

// A pass-through that, with `deep` set, does its block's work as deep in MicroQuickJS's C re-entries as the host lets
// it. A native function that calls back into JavaScript enters the interpreter again through a nested JS_Call, on the C
// stack, and the engine refuses a call past kReentries with an InternalError. String.prototype.replace with a function
// is the native whose C frames between two calls are the largest (js_string_replace and js_string_concat_subst: 200
// bytes on the RP2350 at -Os, where Array.prototype.forEach's js_array_every takes 64). Each block the node calls
// replace from inside replace's callback until the engine refuses, and at the deepest level reads its messages (the
// host's refill, a SysEx's array among it), forwards them and parses a malformed JSON text, whose parser and syntax
// error are the deepest C chain a call there can make: what the device check measures its stack under
// (packages/graph-runtime/device-check's script plan sets `deep`). The deepest is the level whose replace never
// entered its callback; a throw from below it, or from the work there, goes on up, so the node faults. It faults too if
// the host let it nest less or more deeply than kReentries. With `deep` set it passes on every message but the
// real-time ones (F8 to FF), so a run that hears the deep path is told from one that does not; with `deep` off, the
// default (the device check's run of each type, the toolchain's run), it passes every message on from process itself.
export const scriptNestNode = defineNode({
  id: "rp.devtest.script-nest",
  name: "Script nest (devtest)",
  category: "test",
  settings: s.object({ deep: s.bool(false) }),
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: ({ settings }) => {
    const deep = settings.deep;
    let block: Block | null = null;
    let depth = 0;
    let reached = 0;
    let done = false;
    const work = (b: Block): void => {
      const messages = b.messages("in");
      for (let i = 0; i < messages.length; i++) if (messages[i].data[0] < 0xf8) b.forward("out", messages[i]);
      try {
        JSON.parse("{");
      } catch (e) {
        // The syntax error is the point: its parser and its error's backtrace run on the deepest chain.
      }
    };
    const down = (): string => {
      const at = ++depth;
      if (at > reached) reached = at;
      try {
        "x".replace("x", down);
      } catch (e) {
        if (done || reached !== at) throw e;
      }
      if (!done && reached === at) {
        done = true;
        work(block as Block);
      }
      depth--;
      return "";
    };
    return {
      process(b) {
        if (!deep) {
          const shallow = b.messages("in");
          for (let i = 0; i < shallow.length; i++) b.forward("out", shallow[i]);
          return;
        }
        block = b;
        depth = 0;
        reached = 0;
        done = false;
        down();
        block = null;
        if (reached !== kReentries) throw new Error(`nested ${reached} deep, where the host allows ${kReentries}`);
      },
    };
  },
});

// One builtin over data of `count`, called for ever: an array's sort (of `count` numbers), a string's search (in a
// string of `count` characters for a needle of half as many and one more, so every position compares about half of it),
// join of an array-like whose length is `count`, forEach over `count` numbers, JSON.stringify of them, an array's
// indexOf over `count` references to a string of `length` characters for a needle as long that differs at its end (so
// every element compares the whole string), or forEach over `count` elements with a callback that makes three strings
// of about twice `length` each (`loop`; "none", the default, does nothing, so the device check's run of each type and
// the toolchain's pass it). indexOf's and callback's loops end after `rounds` calls when it is not 0 (a test that
// counts the polls). Every call is work the engine's interpreter does not poll its deadline within, so the loop is held
// to its deadline only by the host's polls between calls and the engine's own inside them (graph-runtime's
// script/MqjsHost.hpp, kFirstPollDivisor): what the device check's deadline case times on the box
// (packages/graph-runtime/device-check, loopDeadline) and the device-mode tests time at a pack's declared heaps. Its
// blocks make its data first, the strings in one and kLoopFill elements a block after, well inside a block's deadline
// on the box; then a block runs `warm` turns of a cheap loop (a phase of plain code, which lets the polls grow apart),
// logs a line naming the loop, and loops over the builtin until the deadline faults the node.
const kLoopFill = 256;

export const scriptLoopNode = defineNode({
  id: "rp.devtest.script-loop",
  name: "Script builtin loop (devtest)",
  category: "test",
  settings: s.object({
    loop: s.enum(["none", "sort", "search", "join", "forEach", "stringify", "indexOf", "callback"], "none"),
    count: s.int({ min: 0, max: 1000000000, default: 0 }),
    warm: s.int({ min: 0, max: 1000000000, default: 0 }),
    length: s.int({ min: 1, max: 65536, default: 4096 }),
    rounds: s.int({ min: 0, max: 1000000000, default: 0 }),
  }),
  ports: { out: events.out() },
  create: ({ settings }) => {
    const loop = settings.loop;
    const count = settings.count;
    const rounds = settings.rounds;  // indexOf's and callback's calls, or 0 for ever
    const listed = loop === "sort" || loop === "forEach" || loop === "stringify" || loop === "indexOf" ||
      loop === "callback";
    const numbers = new Array<number | string>(listed ? count : 0);
    let filled = 0;
    let text = "";
    let needle = "";
    const lengthOnly = { length: loop === "join" ? count : 0 } as unknown as string[];
    const visit = (): void => {};
    // A straight-line callback that makes three strings of twice `text` (a collection now and then as the heap fills).
    let made = "";
    const heavy = (): void => {
      made = text + text;
      made = text + made;
      made = made + text;
    };
    return {
      process() {
        // The strings first: a text of `count` characters and a needle of half as many and one more ("search"); a
        // text of `length` characters and a needle as long that differs at its end, so every comparison of the two
        // runs their whole length ("indexOf", whose array holds `count` references to the text, and "callback").
        if (text.length === 0 && (loop === "search" || loop === "indexOf" || loop === "callback")) {
          const length = loop === "search" ? count : settings.length;
          text = "a";
          while (text.length < length) text += text;
          text = text.substring(0, length);
          needle = loop === "search" ? text.substring(0, count >> 1) + "b" : text.substring(0, length - 1) + "b";
          return;
        }
        if (filled < numbers.length) {
          const end = filled + kLoopFill < numbers.length ? filled + kLoopFill : numbers.length;
          for (; filled < end; filled++) numbers[filled] = loop === "indexOf" ? text : count - filled;
          return;
        }
        let odd = 0;
        for (let i = 0; i < settings.warm; i++) odd += i & 1;
        if (odd < 0) return;
        // A line as the loop begins: the device check's host counts it, so a deadline that faulted the node before
        // the loop is told from one in it.
        if (loop !== "none") console.log("rp.devtest.script-loop: " + loop);
        switch (loop) {
          case "sort":
            for (;;) numbers.sort();
          case "search":
            for (;;) text.indexOf(needle);
          case "join":
            for (;;) Array.prototype.join.call(lengthOnly, "");
          case "forEach":
            for (;;) numbers.forEach(visit);
          case "stringify":
            for (;;) JSON.stringify(numbers);
          case "indexOf":
            for (let r = 0; rounds === 0 || r < rounds; r++) numbers.indexOf(needle);
            return;
          case "callback":
            for (let r = 0; rounds === 0 || r < rounds; r++) numbers.forEach(heavy);
            return;
          default:
            return;
        }
      },
    };
  },
});

registerScriptNodes([scriptTransposeNode, scriptNestNode, scriptLoopNode]);
