// The protocol fixture pack's node (docs/downloadable-packs-m2e-plan.md): each routed MIDI message sent on as host MIDI
// with its second byte 7 up, so a test that drives it sees the pack's code ran. Writes no system input, so it sits
// beside any system's own protocol node.
import { defineNode, events, type DefinedNode } from "@retroplug/core/defineNode";

export const echoNode = defineNode({
  id: "rp.tsfix-proto.echo",
  name: "TS fixture echo",
  category: "protocol",
  ports: { in: events.in(), midiOut: events.out() },
  create: () => ({
    process(b) {
      const ms = b.messages("in");
      for (let i = 0; i < ms.length; i++) {
        const d = ms[i].data;
        b.send("midiOut", ms[i].frame, d.length === 3 ? [d[0], (d[1] + 7) & 0x7f, d[2]] : d);
      }
    },
  }),
});

const nodes: readonly DefinedNode<any>[] = [echoNode];
export default nodes;
