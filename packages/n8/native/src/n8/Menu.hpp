#pragma once

// The on-device Everdrive N8 menu's command channel (docs/modular-graph-phase-e2-design.md section 4). While the N8's
// file-browser menu runs it reads '*'-prefixed commands from the same cart FIFO the MIDI bridge writes to, and replies
// over USB. This drives the menu to install and boot a ROM: the menu firmware itself parses the iNES header, sources
// the standard mapper core from its SD and boots the FPGA; the host does none of that. Only valid while the MENU runs:
// after start() the game runs, and a running game cannot be returned to the menu over USB (the console's reset button
// or a power-cycle does it). Portable, as Edio is: a Status from every call, caller buffers, nothing allocated.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "n8/Edio.hpp"

namespace rp::n8 {

class Menu {
public:
    static constexpr std::size_t kVramBytes    = 2048;  // 1024 flattened (tileIdx, attr) pairs
    static constexpr std::size_t kPaletteBytes = 16;    // NES master-palette indices

    explicit Menu(Edio& edio) : edio_(edio) {}

    // The handshake: '*t', answered 'k'. Anything else is BadReply (the byte in Status::reply); no answer is a
    // Timeout, which is what a running game gives (it does not read '*' commands).
    Status test();

    // Select a ROM by its SD path: '*n' and the length-prefixed path, answered by a status byte (0 = ok; 0x44 is the
    // menu's out-of-memory, a dirty heap after a failed load) within the install timeout, as the menu loads the ROM and
    // its mapper core off SD first, then the 16-bit map index, into `mapIndex` when given. The index is read at the
    // reply's 2 s, not the install's: the menu sends it straight after the status (the Pico read it so too). A failure
    // reading it is at Where::MapIndex, after a 0 status: the install itself succeeded.
    Status install(std::string_view path, std::uint16_t* mapIndex = nullptr);

    // Boot the installed ROM: '*s'. The menu core drops out and the game runs; nothing answers.
    Status start();

    // The menu's on-screen state: '*v', answered by 2048 VRAM bytes then 16 palette bytes. Pair it with a
    // memRD(ADDR_MENU_CHR, 8192) to rebuild the 256x224 screen (menuImage.ts). Menu-only: test() first for a clean
    // error.
    Status vramDump(std::uint8_t (&vram)[kVramBytes], std::uint8_t (&palette)[kPaletteBytes]);

    // Reboot to the menu: flushInput, '*r', then a best-effort read of its ack (none, or the console already
    // rebooting, is fine: the caller polls test() to learn when the menu is back). Fails only when '*r' could not be
    // written.
    Status reset();

private:
    Status command(char c);

    Edio& edio_;
};

}  // namespace rp::n8
