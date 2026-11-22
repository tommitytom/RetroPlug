#include "host/packs/PackManifest.hpp"

#include <fstream>
#include <iterator>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// The manifest as JSON spells it. Optional members are checked below, so a missing one is reported by name rather
// than as reflect-cpp's generic message. Unknown top-level members are ignored: a member added at the same API level
// is additive by definition, and an older host of that level still loads the pack whole.
struct RawRequires {
    std::optional<std::vector<std::string>> packs;
    std::optional<std::vector<std::string>> platforms;
};
struct RawProvides {
    std::optional<std::vector<std::string>> platforms;
    std::optional<std::vector<std::string>> nodeTypes;
    std::optional<std::vector<std::string>> roles;
};
struct RawManifest {
    std::optional<std::string>                                id;
    std::optional<std::string>                                name;
    std::optional<std::string>                                version;
    std::optional<std::int64_t>                               apiLevel;
    std::optional<std::string>                                license;
    std::optional<std::string>                                source;
    rfl::Rename<"requires", std::optional<RawRequires>>       requirements;
    std::optional<RawProvides>                                provides;
    std::optional<std::map<std::string, std::string>>         halves;
};

bool refuse(std::string* why, std::string reason) {
    if (why) *why = "pack.json: " + std::move(reason);
    return false;
}

}  // namespace

bool isPackVersion(std::string_view v) {
    int parts = 0;
    std::size_t at = 0;
    while (at <= v.size()) {
        std::size_t end = v.find('.', at);
        if (end == std::string_view::npos) end = v.size();
        const std::string_view part = v.substr(at, end - at);
        if (part.empty() || part.size() > 9) return false;
        if (part.size() > 1 && part[0] == '0') return false;
        for (const char c : part)
            if (c < '0' || c > '9') return false;
        ++parts;
        at = end + 1;
    }
    return parts == 3;
}

namespace {

// A half's path: relative, inside the version folder, '/'-separated, no empty, "." or ".." segment.
bool isInsidePath(std::string_view p) {
    if (p.empty() || p.front() == '/') return false;
    if (p.find('\\') != std::string_view::npos || p.find(':') != std::string_view::npos) return false;
    std::size_t at = 0;
    while (at <= p.size()) {
        std::size_t end = p.find('/', at);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view seg = p.substr(at, end - at);
        if (seg.empty() || seg == "." || seg == "..") return false;
        at = end + 1;
    }
    return true;
}

// A node type, platform or role id as provides spells it: non-empty, printable ASCII with no space or quote.
bool isPlainName(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    for (const char c : s)
        if (c <= ' ' || c > '~' || c == '"' || c == '\\') return false;
    return true;
}

bool checkList(const std::optional<std::vector<std::string>>& list, const char* member, bool packIds,
               std::vector<std::string>& out, std::string* why) {
    if (!list) return true;
    for (const auto& item : *list) {
        if (packIds ? !isPackId(item) : !isPlainName(item))
            return refuse(why, std::string(member) + " holds \"" + item + "\", which is not " +
                                   (packIds ? "a pack id" : "an id"));
        for (const auto& seen : out)
            if (seen == item) return refuse(why, std::string(member) + " names \"" + item + "\" twice");
        out.push_back(item);
    }
    return true;
}

}  // namespace

const std::vector<std::string>& knownHalves() {
    static const std::vector<std::string> kHalves = {"native", "app", "consoles", "protocol", "ui", "cli", "script"};
    return kHalves;
}

std::string PackManifest::nativeHalf() const {
    const auto it = halves.find("native");
    return it == halves.end() ? std::string() : it->second;
}

bool isPackId(std::string_view id) {
    if (id.empty() || id.size() > 64 || id[0] < 'a' || id[0] > 'z') return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why) {
    auto raw = rfl::json::read<RawManifest>(std::string(json));
    if (!raw) {
        refuse(why, std::string("not a manifest: ") + raw.error().what());
        return std::nullopt;
    }
    PackManifest m;
    const auto required = [&](const std::optional<std::string>& value, const char* member, std::string& out) {
        if (!value || value->empty()) return refuse(why, std::string(member) + " is required (a non-empty string)");
        out = *value;
        return true;
    };
    if (!required(raw->id, "id", m.id) || !required(raw->name, "name", m.name) ||
        !required(raw->version, "version", m.version) || !required(raw->license, "license", m.license) ||
        !required(raw->source, "source", m.source))
        return std::nullopt;
    if (!isPackId(m.id)) {
        refuse(why, "id \"" + m.id + "\" is not a pack id (lowercase letters, digits and '-', starting with a letter)");
        return std::nullopt;
    }
    if (!isPackVersion(m.version)) {
        refuse(why, "version \"" + m.version + "\" is not MAJOR.MINOR.PATCH");
        return std::nullopt;
    }
    if (!raw->apiLevel || *raw->apiLevel < 0 || *raw->apiLevel > 0xffffffffll) {
        refuse(why, "apiLevel is required (a whole number)");
        return std::nullopt;
    }
    m.apiLevel = static_cast<std::uint32_t>(*raw->apiLevel);
    if (const auto& r = raw->requirements.value()) {
        if (!checkList(r->packs, "requires.packs", true, m.requiresPacks, why) ||
            !checkList(r->platforms, "requires.platforms", false, m.requiresPlatforms, why))
            return std::nullopt;
    }
    for (const auto& dep : m.requiresPacks)
        if (dep == m.id) {
            refuse(why, "requires.packs names the pack itself");
            return std::nullopt;
        }
    if (const auto& p = raw->provides) {
        if (!checkList(p->platforms, "provides.platforms", false, m.providesPlatforms, why) ||
            !checkList(p->nodeTypes, "provides.nodeTypes", false, m.providesNodeTypes, why) ||
            !checkList(p->roles, "provides.roles", false, m.providesRoles, why))
            return std::nullopt;
    }
    if (raw->halves) {
        for (const auto& [kind, value] : *raw->halves) {
            bool known = false;
            for (const auto& k : knownHalves()) known = known || k == kind;
            if (!known) {
                refuse(why, "halves." + kind + " is no half this RetroPlug loads");
                return std::nullopt;
            }
            if (kind == "native" && value != "rp-" + m.id) {
                refuse(why, "halves.native is \"" + value + "\", and a pack's library is named after its id: rp-" +
                                m.id);
                return std::nullopt;
            }
            if (kind != "native" && !isInsidePath(value)) {
                refuse(why, "halves." + kind + " is \"" + value + "\", which is not a path inside the pack");
                return std::nullopt;
            }
        }
        m.halves = *raw->halves;
    }
    if (m.halves.empty()) {
        refuse(why, "halves names no half: the pack would load nothing");
        return std::nullopt;
    }
    if (!m.nativeHalf().empty() && m.providesNodeTypes.empty()) {
        refuse(why, "halves.native is set but provides.nodeTypes is empty: a native half registers node types");
        return std::nullopt;
    }
    return m;
}

std::optional<PackManifest> readPackManifest(const fs::path& versionDir, std::string* why) {
    const fs::path file = versionDir / "pack.json";
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (why) *why = "pack.json: cannot read " + displayPath(file);
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parsePackManifest(text, why);
}

}  // namespace rp::host::packs
