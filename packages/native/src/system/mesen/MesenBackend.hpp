#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/SystemFactory.hpp"

class SystemBase;

// The Mesen core, one backend spanning multiple platforms. A system node's settings make one of the configs below, and
// which alternative the request carries picks the console: NES, GBA, or Master System / Game Gear, each through its own
// build function (ROM gate, boot, onActivate, state snapshot). GBA runs on Mesen's HLE boot ROM (biosPath left empty).
class MesenBackend final : public SystemBackend {
public:
    std::unique_ptr<SystemBase> build(SystemId id, const SystemBuildRequest& request,
                                      double sampleRate, std::string* why) override;

private:
    // One boot function per console, reached by build()'s config alternative and by nothing else: the ROM gate, then
    // bootMesen (construct, onActivate, reject a refused ROM, enable the state snapshot). cfg and romBytes are consumed;
    // a refusal's reason is in *why when it is given.
    static std::unique_ptr<SystemBase> buildNes(SystemId id, MesenNesConfig cfg,
                                                std::vector<std::uint8_t> romBytes, double sampleRate,
                                                std::string* why);
    static std::unique_ptr<SystemBase> buildGba(SystemId id, MesenGbaConfig cfg,
                                                std::vector<std::uint8_t> romBytes, double sampleRate,
                                                std::string* why);
    static std::unique_ptr<SystemBase> buildSms(SystemId id, MesenSmsConfig cfg,
                                                std::vector<std::uint8_t> romBytes, double sampleRate,
                                                std::string* why);
};
