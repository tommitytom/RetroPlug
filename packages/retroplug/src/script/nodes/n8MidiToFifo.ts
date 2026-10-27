// A NES's MIDI (the `nes-n8-midi` role's DSP behaviour, dspRoles.ts, as a script node): every routed MIDI message goes
// on whole to the core's MIDI input, which feeds the always-attached N8 FIFO (the NES core's NesN8FifoRole iterates
// the bytes into it). The NES twin of rp.mgb.midi: a core with no serial port receives MIDI here instead. Sends the
// view the block handed over, so a block allocates nothing.
//
// Its output is named for the system node's input it is wired to, `midi`, as every protocol node's is; a port id
// names one port, so its input, the routed MIDI the other protocol nodes call `midi`, is `midiIn` here.
import { defineNode, events } from "../../graph/defineNode";
import { CORE_MIDI } from "./ports";

export const N8_MIDI_TO_FIFO_NODE_TYPE = "rp.n8.midi-to-fifo";

export const n8MidiToFifoNode = defineNode({
  id: N8_MIDI_TO_FIFO_NODE_TYPE,
  name: "N8 MIDI to FIFO",
  category: "protocol",
  reshapes: true,
  ports: { midiIn: events.in(), midi: events.out(CORE_MIDI) },
  create: () => ({
    process(b) {
      const messages = b.messages("midiIn");
      for (let i = 0; i < messages.length; i++) b.send("midi", messages[i].frame, messages[i].data);
    },
  }),
});
