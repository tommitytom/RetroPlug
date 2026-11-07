// The package's surface: the mgb role, its ROM provider and its protocol node. The app's own registration is the `app`
// module (./app), which the generated registry imports by name; importing this barrel registers nothing.
export { MGB_ROLE } from "./app";
export { MGB_ROM_PROVIDER } from "./romProvider";
export { MGB_MIDI_NODE_TYPE, mgbMidiNode } from "./mgbMidiNode";
