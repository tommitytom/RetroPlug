#include "system/sameboy/RomSniffer.hpp"

#include <cstring>
#include <string_view>

namespace {

constexpr std::size_t kTitleOffset = 0x0134;
constexpr std::size_t kTitleSize   = 15;
constexpr std::size_t kHeaderEnd   = 0x0143;

// NUL-trim a fixed-width title slice. The cartridge header zero-pads short
// titles, so we narrow `view` to its longest leading non-NUL prefix.
std::string_view trimNul(std::string_view view) {
    if (const std::size_t z = view.find('\0'); z != std::string_view::npos)
        return view.substr(0, z);
    return view;
}

} // namespace

RomKind detectRomKind(const std::vector<std::uint8_t>& rom) {
    if (rom.size() <= kHeaderEnd) return RomKind::Generic;

    const std::string_view title = trimNul(std::string_view(
        reinterpret_cast<const char*>(rom.data() + kTitleOffset), kTitleSize));

    if (title == "MGB") return RomKind::Mgb;
    return RomKind::Generic;
}
