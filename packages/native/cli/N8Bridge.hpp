#pragma once

namespace retroplug {

// Runs the `retroplug-cli n8-bridge` subcommand: pipe live MIDI input straight to a physical Everdrive N8
// Pro over USB (no emulator, lowest latency), so a controller / DAW plays the real NES running EverMIDI.
// Owns its own loop (SIGINT to stop) - it cannot ride the CLI's bounded QuickJS pump. `argv` is the full
// process arg vector (argv[1] == "n8-bridge"). Returns a process exit code.
int runN8Bridge(int argc, char** argv);

}  // namespace retroplug
