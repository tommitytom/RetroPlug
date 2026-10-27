// The host's MIDI fanned out to the systems (what the DSP kernel's `midi-routing` stage ran until B3b, as a script node):
// one output a system, named by its id, in slot order, since the modes but send-to-all route by a channel's position
// among them. Each message goes to its systems at its own frame, in the order it came, by routeTarget's arithmetic
// (midiRouting.ts), the decision the kernel's routeBlockInto makes; MidiChannelToInstance's target takes a copy with
// the channel rewritten to 0.
//
// Allocation-free: a message goes on as the view the block handed over, and a rewritten one from a 4-byte buffer
// made once, through a view of each length made with it. Only a settings change makes the port ids again.
import { defineNode, events, type PortSpec } from "../../graph/defineNode";
import { s, type Infer } from "../../graph/settingsSchema";
import { MIDI_DATA_SIZE, MIDI_ROUTING_VALUES, MidiRouting, ROUTE_ALL, routeTarget } from "../../midiRouting";
import { ROUTED_MIDI } from "./ports";

export const MIDI_ROUTE_NODE_TYPE = "rp.midi.route";

const settings = s.object({
  mode: s.enum(MIDI_ROUTING_VALUES, "sendToAll"),
  /** The systems' ids, in slot order: each has an output, `sys-<id>`, in this order. */
  systems: s.array(s.int({ min: 1, max: 0xffffffff, default: 1 }), []),
});

export type MidiRouteSettings = Infer<typeof settings>;

/** The output a system's MIDI leaves by. */
export const routedPort = (system: number): string => `sys-${system}`;

const outputsOf = (systems: readonly number[]): string[] => systems.map(routedPort);

export const midiRouteNode = defineNode<MidiRouteSettings>({
  id: MIDI_ROUTE_NODE_TYPE,
  name: "MIDI Routing",
  category: "midi",
  settings,
  reshapes: true,
  // Two systems with one id would be one output fed twice, so the layout refuses them (the settings schema has no
  // way to say so, where the kernel's zod settings refined it).
  ports: (s) => {
    const p: Record<string, PortSpec> = { midiIn: events.in() };
    for (const id of s.systems) {
      const port = routedPort(id);
      if (Object.prototype.hasOwnProperty.call(p, port)) throw new Error(`${MIDI_ROUTE_NODE_TYPE}: two systems have the id ${id}`);
      p[port] = events.out(ROUTED_MIDI);
    }
    return p;
  },
  create: ({ settings: initial }) => {
    let mode = initial.mode;
    let outputs = outputsOf(initial.systems);
    const rewritten = new Uint8Array(MIDI_DATA_SIZE);
    const byLength: Uint8Array[] = [];
    for (let n = 0; n <= MIDI_DATA_SIZE; n++) byLength.push(rewritten.subarray(0, n));
    return {
      process(b) {
        const count = outputs.length;
        if (count === 0) return;
        const messages = b.messages("midiIn");
        for (let e = 0; e < messages.length; e++) {
          const m = messages[e];
          const data = m.data;
          const target = routeTarget(data, mode, count);
          if (target === ROUTE_ALL) {
            for (let i = 0; i < count; i++) b.send(outputs[i], m.frame, data);
          } else if (target >= 0) {
            if (mode === MidiRouting.MidiChannelToInstance) {
              // Only a channel message reaches here in this mode (SysEx and system messages go to every system),
              // so it is at most MIDI_DATA_SIZE bytes.
              rewritten[0] = data[0] & 0xf0;
              for (let j = 1; j < data.length; j++) rewritten[j] = data[j];
              b.send(outputs[target], m.frame, byLength[data.length]);
            } else {
              b.send(outputs[target], m.frame, data);
            }
          }
        }
      },
      onSettings(next) {
        mode = next.mode;
        outputs = outputsOf(next.systems);
      },
    };
  },
});
