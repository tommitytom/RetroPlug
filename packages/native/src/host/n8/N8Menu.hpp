#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace retroplug {

class Edio;

// The menu's on-screen state (from N8Menu::vramDump): 2048 flattened (tileIdx, attr) VRAM pairs + 16
// NES-palette indices. Pair with Edio::memRD(ADDR_MENU_CHR, 8192) to reconstruct the 256x224 screen.
struct N8VramDump {
    std::vector<std::uint8_t> vram;     // 2048 bytes
    std::vector<std::uint8_t> palette;  // 16 bytes
};

// The on-device Everdrive N8 menu command channel - a native twin of the TS N8Menu (n8Menu.ts). While the
// N8's file-browser menu is running it reads '*'-prefixed commands from the same cart FIFO the MIDI bridge
// writes to, and replies over USB. This drives the menu to install + boot a ROM: the menu firmware itself
// parses the iNES header, sources the standard mapper core from its SD, and boots the FPGA - the host does
// none of that. Only valid while the MENU is running; after appStart() the game runs (to switch ROMs the
// console must be reset back to the menu first). Used by the UI SD-op worker (N8SdWorker) over an Edio.
class N8Menu {
public:
    explicit N8Menu(Edio& edio) : edio_(edio) {}

    // Handshake with the menu: '*t' -> expect 'k'. Throws std::runtime_error if the menu isn't running /
    // doesn't answer (a running game won't reply).
    void test();

    // Select a ROM by its device (SD) path: '*n' + length-prefixed path -> status(0=ok) -> 16-bit map index.
    // The menu loads the ROM (iNES parse, PRG/CHR, mapper core from SD). Throws on a device error; returns
    // the map index.
    int appInstall(const std::string& devicePath);

    // Boot the installed ROM: '*s'. The menu core drops out and the game runs.
    void appStart();

    // Dump the menu's on-screen state: '*v' -> the firmware streams 2048 VRAM bytes then 16 palette bytes
    // straight back over USB. Menu-only (a running game won't answer) - call test() first for a clean error.
    N8VramDump vramDump();

    // Reboot back to the menu: '*r' -> best-effort ack (the console then reboots for ~seconds). Not used by
    // the UI ops (we cannot reliably wait for the reboot), kept for parity with n8Menu.ts.
    void reset();

private:
    void cmd(char c);

    Edio& edio_;
};

}  // namespace retroplug
