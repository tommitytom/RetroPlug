#include "CartVolume.hpp"

#include <cstdint>
#include <cstdio>

namespace rp::box {

bool writeExpansionVolume(rp::n8::Edio& edio, int value, bool again, VolumePrint print) {
    char               line[128];
    const std::uint8_t v = static_cast<std::uint8_t>(value);
    if (!edio.memWR(rp::n8::Edio::ADDR_EXP_VOL, &v, 1).ok()) {
        if (!again && print) {
            std::snprintf(line, sizeof line,
                          "[n8-host] cart link: expansion volume %d: write did not go through, "
                          "trying again each second",
                          value);
            print(line);
        }
        return false;
    }
    std::uint8_t back = 0;
    if (!edio.memRD(rp::n8::Edio::ADDR_EXP_VOL, &back, 1).ok())
        std::snprintf(line, sizeof line, "[n8-host] cart link: expansion volume %d written (no read-back)", value);
    else if (back != v)
        std::snprintf(line, sizeof line, "[n8-host] cart link: expansion volume %d written, but reads back %u", value,
                      static_cast<unsigned>(back));
    else
        std::snprintf(line, sizeof line, "[n8-host] cart link: expansion volume %d written (reads back %u)", value,
                      static_cast<unsigned>(back));
    if (print) print(line);
    return true;
}

}  // namespace rp::box
