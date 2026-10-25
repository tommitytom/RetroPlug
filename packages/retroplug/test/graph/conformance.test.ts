// The cross-language pact: these TS nodes and their C++ twins in packages/native/test/graph/NodeLayer.test.cpp
// declare the same shapes, and both must spell the same descriptor bytes - the shared golden files. The C++
// test compares its output to the file text; this one compares canonical spellings, and the
// canonical-json tests prove the two writers spell identically.
import { test, expect } from "../../testing/harness";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { audio, bytes, channels, control, defineNode, events, link, param, resource } from "../../src/graph/defineNode";
import golden from "../../../native/test/graph/golden/rp.test.gain.descriptor.json";
import kitchenGolden from "../../../native/test/graph/golden/rp.test.kitchen-sink.descriptor.json";

const gain = defineNode({
  id: "rp.test.gain",
  name: "Gain",
  category: "dsp/utility",
  ports: { in: audio.in(), midi: events.in(), out: audio.out(channels.match("in")) },
  params: {
    gain: param.float({ name: "Gain", min: -60, max: 12, default: 0, unit: "dB" }),
    mode: param.enum({ name: "Mode", values: ["off", "midi-sync", "high-pass-tpdf"], default: "midi-sync" }),
    mute: param.bool({ name: "Mute" }),
    steps: param.int({ name: "Steps", min: 1, max: 16, default: 4 }),
  },
  create: () => ({ process() {} }),
});

test("the TS gain's descriptor is the C++ gain's descriptor", () => {
  expect(canonicalJson(gain.descriptor())).toBe(canonicalJson(golden));
});

// Every descriptor feature at once. Inputs and outputs are declared interleaved: the descriptor still lists
// inputs first, each side in declaration order, as the C++ struct-of-structs does.
const kitchenSink = defineNode({
  id: "rp.test.kitchen-sink",
  name: "Kitchen Sink",
  category: "test/conformance",
  version: "2.1.0",
  requires: { heap: true },
  ports: {
    in: audio.in(),
    out: audio.out(channels.match("in")),
    side: audio.in(channels.fixed(2, "any")),
    surround: audio.out(channels.fixed(6, "5.1")),
    midi: events.in({ messages: 512 }),
    notes: events.out({ messages: 1024 }),
    serial: bytes.in({ bytes: 8192 }),
    serialOut: bytes.out({ messages: 64, bytes: 65536 }),
    mod: control.in(),
    level: control.out(),
    peer: link(),
    sample: resource.in("audio-buffer"),
    target: resource.write("audio-buffer"),
    buffer: resource.out("audio-buffer"),
    "pins.a": audio.out(channels.mono()),
    "pins.b": audio.out(channels.mono()),
    "channels.x": audio.out(channels.mono()),
  },
  params: {
    tiny: param.float({ name: "Tiny", min: 0, max: 1e-7, default: 1e-7 }),
    huge: param.float({ name: "Huge", min: -1e21, max: 1e21, default: 0 }),
    steps: param.int({ name: "Steps", min: 1, max: 16, default: 4, unit: "steps" }),
    armed: param.bool({ name: "Armed", default: true }),
    sync: param.enum({ name: "Sync", values: ["off", "midi-sync", "n8-fifo"], default: "n8-fifo" }),
  },
  exclusive: [["pins.*", "channels.*"]],
  create: () => ({ process() {} }),
});

test("every descriptor feature spells the same in TS and C++", () => {
  expect(canonicalJson(kitchenSink.descriptor())).toBe(canonicalJson(kitchenGolden));
});
