#include "host/n8/N8Menu.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

#include "host/n8/Edio.hpp"

namespace retroplug {

void N8Menu::cmd(char c) {
    const std::uint8_t buff[2] = { static_cast<std::uint8_t>('*'), static_cast<std::uint8_t>(c) };
    edio_.fifoWR(buff, 2);
}

void N8Menu::test() {
    cmd('t');
    const std::uint8_t resp = edio_.rx8();  // throws on timeout (menu not running / not listening)
    if (resp != 'k') {
        char msg[80];
        std::snprintf(msg, sizeof(msg), "N8 menu: unexpected test response 0x%02X (is the menu running?)", resp);
        throw std::runtime_error(msg);
    }
}

int N8Menu::appInstall(const std::string& devicePath) {
    edio_.setReadTimeout(10000);  // the menu loads the ROM + FPGA core from SD before replying
    cmd('n');
    edio_.fifoTxString(devicePath);
    const std::uint8_t status = edio_.rx8();  // game-select status
    if (status == 0x44) {  // ERR_OUT_OF_MEMORY: the menu heap is dirty (usually after a prior failed load)
        throw std::runtime_error(
            "N8 menu out of memory (0x44) - the menu heap is dirty (e.g. after a prior failed load). "
            "Power-cycle the console to a fresh menu and retry.");
    }
    if (status != 0) {
        char msg[80];
        std::snprintf(msg, sizeof(msg), "N8 menu: app install error 0x%02X (path '%s')", status, devicePath.c_str());
        throw std::runtime_error(msg);
    }
    return edio_.rx16();  // map index
}

void N8Menu::appStart() {
    cmd('s');
}

void N8Menu::reset() {
    // '*r' reboots the console into a fresh menu (clears a dirty heap -> avoids ERR_OUT_OF_MEMORY). The byte
    // that follows is just a command ack; the console then reboots (~seconds, screen goes black), so the
    // caller must poll test() to know when the menu is actually back. Consume the ack best-effort.
    edio_.flushInput();
    cmd('r');
    edio_.setReadTimeout(2000);
    try {
        edio_.rx8();
    } catch (const std::exception&) {
        // No ack / already rebooting - fine; readiness is confirmed by the caller polling test().
    }
}

}  // namespace retroplug
