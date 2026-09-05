#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace rp {

// One symbol from a cc65 `.dbg` debug-info file: a name and the CPU-space
// address it resolves to. `cName` marks a C-level name (from a `csym` record:
// `g_frame`, or a file-scope `static` like `s_mode1`) resolved through its
// assembler label; the assembler labels themselves (`_g_frame`, `midiIdleLoop`)
// have `cName` false and are what Mesen's label manager is fed.
struct DbgSymbol {
    std::string   name;
    std::uint32_t address = 0;
    bool          cName   = false;
};

namespace detail {

// A record's comma-separated `key=value` fields (string values quoted). Pure
// string slicing; unknown keys are kept, so a caller picks what it needs.
inline std::unordered_map<std::string, std::string> dbgFields(const std::string& fields) {
    std::unordered_map<std::string, std::string> out;
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
        out[key] = value;
    }
    return out;
}

} // namespace detail

// Parse a cc65 `.dbg` file. The format is one record per line, a tab between
// the record type and a comma-separated list of key=value fields:
//
//   sym\tid=835,name="_g_frame",addrsize=absolute,scope=7,...,val=0x356,seg=2,type=lab
//   sym\tid=12,name="_g_frame",addrsize=absolute,scope=0,...,type=imp,exp=1406
//   csym\tid=2,name="g_frame",scope=7,type=5,sc=ext,sym=835
//   csym\tid=4,name="vel_scale",scope=8,type=5,sc=static,sym=877
//
// Returns, in this order: every `sym` of `type=lab` that carries a `val`
// (the assembler labels, `cName` false), then every `csym` whose `sym` chain
// leads to an address (`cName` true) - an `imp` sym carries no `val` but an
// `exp=` pointing at the defining sym, which is followed. Statics DO have an
// address here: cc65 emits a file-scope static as an assembler label too
// (`_s_mode1`, `type=lab`, with `val`), only its `csym` says `sc=static`; a
// name-regex over `name="s_mode1"` misses it because that record has no
// `val`, which is what this resolution is for. Returns empty on read failure.
// Source-line records (`line`/`span`/`file`) are deliberately not parsed.
inline std::vector<DbgSymbol> parseCc65Dbg(const std::string& path) {
    std::vector<DbgSymbol> out;
    std::ifstream in(path);
    if (!in) return out;

    struct Sym { std::string name; long val = -1; long exp = -1; bool lab = false; };
    std::unordered_map<long, Sym> syms;                       // by id
    std::vector<std::pair<std::string, long>> csyms;          // (C name, sym id), file order

    std::string line;
    while (std::getline(in, line)) {
        const std::size_t sep = line.find_first_of(" \t");
        if (sep == std::string::npos) continue;
        const std::string kind = line.substr(0, sep);
        if (kind != "sym" && kind != "csym") continue;
        const auto f = detail::dbgFields(line.substr(sep + 1));
        const auto get = [&](const char* k) -> const std::string* {
            auto it = f.find(k);
            return it == f.end() ? nullptr : &it->second;
        };
        const std::string* id = get("id");
        const std::string* name = get("name");
        if (!id || !name || name->empty()) continue;
        if (kind == "sym") {
            Sym s;
            s.name = *name;
            if (const std::string* v = get("val")) s.val = std::strtol(v->c_str(), nullptr, 0);  // 0x.. -> hex
            if (const std::string* e = get("exp")) s.exp = std::strtol(e->c_str(), nullptr, 10);
            if (const std::string* t = get("type")) s.lab = t->find("lab") != std::string::npos;
            const long sid = std::strtol(id->c_str(), nullptr, 10);
            if (s.lab && s.val >= 0) out.push_back({ s.name, static_cast<std::uint32_t>(s.val), false });
            syms.emplace(sid, std::move(s));
        } else if (const std::string* symId = get("sym")) {
            csyms.emplace_back(*name, std::strtol(symId->c_str(), nullptr, 10));
        }
    }

    // Resolve each C name: its sym, following `exp` (an import -> the export that defines it) a few
    // hops at most, to a sym with a value.
    for (const auto& [cname, sid] : csyms) {
        long cur = sid;
        for (int hop = 0; hop < 8 && cur >= 0; ++hop) {
            auto it = syms.find(cur);
            if (it == syms.end()) break;
            if (it->second.val >= 0) {
                out.push_back({ cname, static_cast<std::uint32_t>(it->second.val), true });
                break;
            }
            cur = it->second.exp;
        }
    }
    return out;
}

} // namespace rp
