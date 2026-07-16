#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace rp {

// One code/data label from a cc65 `.dbg` debug-info file: a symbol name and the
// address it resolves to (the symbol's `val`, a CPU-space address).
struct DbgSymbol {
    std::string   name;
    std::uint32_t address = 0;
};

// Parse a cc65 `.dbg` file and return its `type=lab` symbols (function / data
// labels) as (name, value). The format is one record per line:
//
//   sym\tid=0,name="midiPoll",addrsize=absolute,scope=0,val=0x9C69,seg=0,type=lab
//
// Tab between the record type and a comma-separated list of key=value fields
// (string values quoted). We only consume `sym` records with `type` containing
// "lab"; everything else (line/span/scope/seg/…) is ignored. Returns empty on
// read failure. Source-line records (`line`/`span`/`file`) are deliberately not
// parsed yet — function names are enough for the profiler; line mapping can be
// layered on later.
inline std::vector<DbgSymbol> parseCc65Dbg(const std::string& path) {
    std::vector<DbgSymbol> out;
    std::ifstream in(path);
    if (!in) return out;

    std::string line;
    while (std::getline(in, line)) {
        const std::size_t sep = line.find_first_of(" \t");
        if (sep == std::string::npos) continue;
        if (line.compare(0, sep, "sym") != 0) continue;

        const std::string fields = line.substr(sep + 1);
        std::string name, type;
        long val = -1;

        std::size_t i = 0;
        while (i < fields.size()) {
            const std::size_t eq = fields.find('=', i);
            if (eq == std::string::npos) break;
            const std::string key = fields.substr(i, eq - i);

            std::size_t vstart = eq + 1;
            std::string value;
            if (vstart < fields.size() && fields[vstart] == '"') {
                const std::size_t close = fields.find('"', vstart + 1);
                value = fields.substr(vstart + 1,
                    (close == std::string::npos ? fields.size() : close) - (vstart + 1));
                const std::size_t comma = fields.find(',', close == std::string::npos ? fields.size() : close);
                i = (comma == std::string::npos) ? fields.size() : comma + 1;
            } else {
                const std::size_t comma = fields.find(',', vstart);
                value = fields.substr(vstart, (comma == std::string::npos ? fields.size() : comma) - vstart);
                i = (comma == std::string::npos) ? fields.size() : comma + 1;
            }

            if (key == "name")      name = value;
            else if (key == "type") type = value;
            else if (key == "val")  val = std::strtol(value.c_str(), nullptr, 0); // 0x.. -> hex
        }

        if (val >= 0 && !name.empty() && type.find("lab") != std::string::npos)
            out.push_back({ name, static_cast<std::uint32_t>(val) });
    }
    return out;
}

} // namespace rp
