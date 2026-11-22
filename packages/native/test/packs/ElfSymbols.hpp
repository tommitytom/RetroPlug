#pragma once

// A pack module's dynamic symbols, read from its ELF file (Linux only): what it exports, and what it imports, so a
// case can hold a module to exporting rp_pack_entry alone and importing nothing of RetroPlug's, the two properties
// cmake/packs.cmake's version script and --no-undefined give it. Reads the section headers for SHT_DYNSYM and the
// string table it links, which every shared object the toolchain makes has.

#if defined(__linux__)

#include <elf.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace rp::host::packs::test {

struct DynamicSymbols {
    // Defined, global, weak or GNU-unique, with any visibility but hidden or internal (protected counts: it is in the
    // dynamic table for others to bind to). A GNU-unique symbol (an inline function's static,
    // a template's static member) is merged process-wide by glibc even under RTLD_LOCAL: the one way two copies'
    // statics really become one, so it counts as an export.
    std::vector<std::string> exported;
    std::vector<std::string> imported;  // undefined, global (a weak undefined one is optional and left out)
};

inline std::optional<DynamicSymbols> readDynamicSymbols(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(Elf64_Ehdr) || std::memcmp(bytes.data(), ELFMAG, SELFMAG) != 0 ||
        bytes[EI_CLASS] != ELFCLASS64)
        return std::nullopt;
    Elf64_Ehdr eh;
    std::memcpy(&eh, bytes.data(), sizeof eh);
    if (eh.e_shentsize < sizeof(Elf64_Shdr)) return std::nullopt;
    const auto section = [&](std::size_t i) {
        Elf64_Shdr sh;
        std::memcpy(&sh, bytes.data() + eh.e_shoff + i * eh.e_shentsize, sizeof sh);
        return sh;
    };
    if (eh.e_shoff + std::size_t{eh.e_shnum} * eh.e_shentsize > bytes.size()) return std::nullopt;
    for (std::size_t i = 0; i < eh.e_shnum; ++i) {
        const Elf64_Shdr dynsym = section(i);
        if (dynsym.sh_type != SHT_DYNSYM || dynsym.sh_entsize != sizeof(Elf64_Sym)) continue;
        if (dynsym.sh_link >= eh.e_shnum) return std::nullopt;
        const Elf64_Shdr strtab = section(dynsym.sh_link);
        if (dynsym.sh_offset + dynsym.sh_size > bytes.size() || strtab.sh_offset + strtab.sh_size > bytes.size())
            return std::nullopt;
        DynamicSymbols out;
        for (std::size_t k = 1; k < dynsym.sh_size / sizeof(Elf64_Sym); ++k) {  // entry 0 is the null symbol
            Elf64_Sym sym;
            std::memcpy(&sym, bytes.data() + dynsym.sh_offset + k * sizeof sym, sizeof sym);
            if (sym.st_name >= strtab.sh_size) return std::nullopt;
            const std::string name(bytes.data() + strtab.sh_offset + sym.st_name);
            const unsigned bind = ELF64_ST_BIND(sym.st_info);
            if (sym.st_shndx == SHN_UNDEF) {
                if (bind == STB_GLOBAL) out.imported.push_back(name);
            } else if ((bind == STB_GLOBAL || bind == STB_WEAK || bind == STB_GNU_UNIQUE) &&
                       ELF64_ST_VISIBILITY(sym.st_other) != STV_HIDDEN &&
                       ELF64_ST_VISIBILITY(sym.st_other) != STV_INTERNAL) {
                out.exported.push_back(name);
            }
        }
        return out;
    }
    return std::nullopt;
}

// Whether an imported name is RetroPlug's: a C symbol of ours (rp_...), or anything mangled that names namespace rp
// (its functions, vtables, typeinfo and guards all carry the nested name N2rp).
inline bool isRetroPlugSymbol(const std::string& name) {
    return name.rfind("rp_", 0) == 0 || (name.rfind("_Z", 0) == 0 && name.find("N2rp") != std::string::npos);
}

}  // namespace rp::host::packs::test

#endif  // __linux__
