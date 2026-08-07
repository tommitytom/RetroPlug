#pragma once

namespace retroplug {

// Runs `retroplug-cli n8-load`: load + boot a ROM on a physical Everdrive N8 Pro over USB by driving its
// on-device menu (the N8 firmware parses the ROM + sources the mapper core from its own SD - the host does
// no low-level FPGA/mapper work). `argv` is the full process arg vector (argv[1] == "n8-load"). Returns a
// process exit code.
int runN8Load(int argc, char** argv);

}  // namespace retroplug
