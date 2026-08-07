#pragma once

#include <string>

namespace retroplug {

class Edio;

// The on-device Everdrive N8 menu command channel (ported from edlink DEV_EDN8/MenuCmd.cs). While the N8's
// file-browser menu is running, it reads '*'-prefixed commands from the same cart FIFO the MIDI bridge
// writes to, and replies over the USB read path. This drives the menu to install + boot a ROM: the menu
// firmware itself parses the iNES header, loads PRG/CHR, sources the standard mapper core from its SD, builds
// MapConfig, and boots the FPGA - the host does none of that. Only valid while the MENU is running; after
// appStart() the game runs instead (to switch ROMs, reset() back to the menu first).
class N8Menu {
public:
    explicit N8Menu(Edio& edio) : edio_(edio) {}

    // Handshake with the menu: '*t' -> expect 'k'. Throws if the menu isn't running / doesn't answer.
    void test();

    // Select a ROM by its device (SD) path: '*n' + length-prefixed path -> status(0=ok) -> 16-bit map index.
    // The menu loads the ROM (iNES parse, PRG/CHR, mapper core from SD, MapConfig). Returns the map index.
    // Loading can take a couple of seconds, so this raises the read timeout while it waits.
    int appInstall(const std::string& devicePath);

    // Boot the installed ROM: '*s'. The menu core drops out and the game runs.
    void appStart();

    // Reboot back to the menu: '*r' -> wait for ready. Use to switch ROMs.
    void reset();

private:
    void cmd(char c);  // fifoWR({'*', c})

    Edio& edio_;
};

}  // namespace retroplug
