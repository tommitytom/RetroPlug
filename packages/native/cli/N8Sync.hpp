#pragma once

namespace retroplug {

// Runs the `retroplug-cli n8-sync` subcommand: translate an incoming MIDI clock/transport into risa's
// host-sync byte protocol (arm+locate / start / 24-PPQN clock / stop) and stream it to a physical Everdrive
// N8 Pro running the risa NES tracker, so the real cart follows the DAW/sequencer transport. Sibling to
// n8-bridge (which raw-forwards MIDI notes for EverMIDI); this one is protocol-translating, for risa. Owns
// its own loop (SIGINT to stop). `argv` is the full process arg vector (argv[1] == "n8-sync"). Returns a
// process exit code.
int runN8Sync(int argc, char** argv);

}  // namespace retroplug
