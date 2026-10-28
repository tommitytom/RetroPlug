// mGB's MIDI (the `mgb` role's audio-thread work; dspRoles.ts registers the role, and buildGraph places this node for
// it): every routed MIDI byte goes into the Game Boy's serial input at its message's frame, which is all mGB listens
// for. A message goes as one serial-in message of its bytes, where the kernel sent a run of same-frame bytes as one:
// the system node applies a message's bytes in order at its frame, so the bytes the core sees, and when, are the same.
// Sends the view the block handed over, so a block allocates nothing.
import { bytes, defineNode, events } from "../../graph/defineNode";
import { SERIAL_IN } from "./ports";

export const MGB_MIDI_NODE_TYPE = "rp.mgb.midi";

export const mgbMidiNode = defineNode({
  id: MGB_MIDI_NODE_TYPE,
  name: "mGB MIDI",
  category: "protocol",
  reshapes: true,
  ports: { midi: events.in(), serialIn: bytes.out(SERIAL_IN) },
  create: () => ({
    process(b) {
      const messages = b.messages("midi");
      for (let i = 0; i < messages.length; i++) {
        const m = messages[i];
        // The kernel's mgb pushed each byte of a message, so an empty message pushed nothing: it is skipped here.
        if (m.data.length > 0) b.send("serialIn", m.frame, m.data);
      }
    },
  }),
});
