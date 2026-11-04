#include "n8/Menu.hpp"

namespace rp::n8 {

Status Menu::command(char c) {
    const std::uint8_t bytes[2] = {0x2a /* '*' */, static_cast<std::uint8_t>(c)};
    return edio_.fifoWR(bytes, 2);
}

Status Menu::test() {
    if (Status s = command('t'); !s.ok()) return s;
    std::uint8_t resp = 0;
    if (Status s = edio_.rx8(resp); !s.ok()) return s;  // a timeout: the menu is not running (or not listening)
    if (resp == 0x6b /* 'k' */) return {};
    Status s;
    s.code  = Code::BadReply;
    s.where = Where::MenuTest;
    s.reply = resp;
    return s;
}

Status Menu::install(std::string_view path, std::uint16_t* mapIndex) {
    if (Status s = command('n'); !s.ok()) return s;
    if (Status s = edio_.fifoTxString(path); !s.ok()) return s;
    std::uint8_t status = 0;  // the game-select status, once the menu has loaded the ROM and its core off SD
    if (Status s = edio_.rx8(status, edio_.timeouts().installMs); !s.ok()) return s;
    if (status != 0) {
        Status s;
        s.code   = Code::Device;
        s.where  = Where::Install;
        s.device = status;
        return s;
    }
    std::uint16_t index = 0;
    if (Status s = edio_.rx16(index); !s.ok()) {
        s.where = Where::MapIndex;  // the ROM is installed: a caller may still start it (the Pico box does)
        return s;
    }
    if (mapIndex) *mapIndex = index;
    return {};
}

Status Menu::start() { return command('s'); }

Status Menu::vramDump(std::uint8_t (&vram)[kVramBytes], std::uint8_t (&palette)[kPaletteBytes]) {
    if (Status s = command('v'); !s.ok()) return s;
    if (Status s = edio_.readData(vram, kVramBytes); !s.ok()) return s;
    return edio_.readData(palette, kPaletteBytes);
}

Status Menu::reset() {
    edio_.flushInput();
    if (Status s = command('r'); !s.ok()) return s;
    std::uint8_t ack = 0;
    (void)edio_.rx8(ack);  // no ack, or already rebooting: fine, readiness is the caller's test() poll
    return {};
}

}  // namespace rp::n8
