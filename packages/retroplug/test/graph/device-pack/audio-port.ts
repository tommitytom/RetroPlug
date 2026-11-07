// A device pack with a node that has an audio port: defineNode must refuse it at definition, naming the node and the
// port, and so the device-pack toolchain refuses the pack (test/graph/device-pack.test.ts).
import { audio, channels, defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

export const midiToGate = defineNode({
  id: "rp.test.device.midi-to-gate",
  ports: { midi: events.in(), gate: audio.out(channels.mono()) },
  create: () => ({ process() {} }),
});

registerScriptNodes([midiToGate]);
