#include "host/n8/N8Menu.hpp"

#include <cstdint>
#include <stdexcept>

#include "host/n8/Edio.hpp"

namespace retroplug {

void N8Menu::test() {
    cmd('t');
    const std::uint8_t resp = edio_.rx8();  // throws on timeout (menu not running / not listening)
    if (resp != 0x6b /* 'k' */)
        throw std::runtime_error("N8 is not at its menu (no '*t' answer) - reset the cart to the file browser and retry");
}

int N8Menu::appInstall(const std::string& devicePath) {
    edio_.setReadTimeout(10000);  // the menu loads the ROM + FPGA core from SD before replying
    cmd('n');
    edio_.fifoTxString(devicePath);
    const std::uint8_t status = edio_.rx8();  // game-select status
    if (status == 0x44)                       // ERR_OUT_OF_MEMORY: the menu heap is dirty (prior failed load)
        throw std::runtime_error(
            "N8 menu out of memory (0x44) - the menu heap is dirty (e.g. after a prior failed load). "
            "Power-cycle the console to a fresh menu and retry.");
    if (status != 0)
        throw std::runtime_error("N8 menu: app install error (path '" + devicePath + "')");
    return edio_.rx16();  // map index
}

void N8Menu::appStart() { cmd('s'); }

N8VramDump N8Menu::vramDump() {
    cmd('v');
    N8VramDump out;
    out.vram.resize(2048);
    out.palette.resize(16);
    edio_.readData(out.vram.data(), out.vram.size());
    edio_.readData(out.palette.data(), out.palette.size());
    return out;
}

void N8Menu::reset() {
    edio_.flushInput();
    cmd('r');
    edio_.setReadTimeout(2000);
    try {
        edio_.rx8();
    } catch (...) {
        // no ack / already rebooting - fine; readiness is confirmed by the caller polling test()
    }
}

void N8Menu::cmd(char c) {
    const std::uint8_t bytes[2] = {0x2a /* '*' */, static_cast<std::uint8_t>(c)};
    edio_.fifoWR(bytes, 2);
}

}  // namespace retroplug
