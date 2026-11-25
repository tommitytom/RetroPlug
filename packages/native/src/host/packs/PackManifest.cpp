#include "host/packs/PackManifest.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <unordered_set>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "graph/CanonicalJson.hpp"
#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

// The structs reflect-cpp reads live in a named namespace: it converts through a fake object of each type, which Apple
// clang refuses ("used but not defined") for a type of internal linkage.
namespace detail::manifest {

// The manifest as JSON spells it. Optional members are checked below, so a missing one is reported by name rather
// than as reflect-cpp's generic message. Unknown top-level members are ignored: a member added at the same API level
// is additive by definition, and an older host of that level still loads the pack whole.
//
// `requires` and `provides` are closed: a member they do not know (a misspelt "pack" for "packs") would otherwise parse
// as an empty list and drop a dependency without a word, so `extra` catches it and the parse refuses it by name.
struct RawRequires {
    std::optional<std::vector<std::string>> packs;
    std::optional<std::vector<std::string>> platforms;
    rfl::ExtraFields<rfl::Generic>          extra;
};
struct RawProvides {
    std::optional<std::vector<std::string>> platforms;
    std::optional<std::vector<std::string>> nodeTypes;
    std::optional<std::vector<std::string>> roles;
    rfl::ExtraFields<rfl::Generic>          extra;
};
// Just the members the level gate reads, so a manifest of another level is judged before the rest of it is.
struct LevelPeek {
    std::optional<std::string>  id;
    std::optional<std::int64_t> apiLevel;
};
// `script` is closed as requires and provides are: a misspelt member would otherwise drop a field without a word.
struct RawScript {
    std::optional<std::string>              runtime;
    std::optional<std::int64_t>             heapBytes;
    std::optional<std::vector<std::string>> nodeTypes;
    rfl::ExtraFields<rfl::Generic>          extra;
};
// One protocol node: its type, and everything else kept for the TS loader, whose checks hold the wiring. Its label is
// refused here: the host gives a pack's script nodes their span labels at load.
struct RawProtocolNode {
    std::optional<std::string>     type;
    rfl::ExtraFields<rfl::Generic> extra;
};
// One kind item: its id, and everything else kept as its data, which the TS loader holds to the declared kind's keys.
struct RawKindItem {
    std::optional<std::string>     id;
    rfl::ExtraFields<rfl::Generic> extra;
};
// `protocol` is closed as `script` is, and names its one refused member, kinds, apart (it is a top-level member).
struct RawProtocol {
    std::optional<std::vector<rfl::Generic>>          nodes;
    std::optional<std::map<std::string, std::string>> roles;
    rfl::ExtraFields<rfl::Generic>                    extra;
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
    std::optional<RawScript>                                  script;
    std::optional<RawProtocol>                                protocol;
    std::optional<std::map<std::string, std::vector<rfl::Generic>>> kinds;
};

}  // namespace detail::manifest

using namespace detail::manifest;

namespace {

// The largest pack.json read: a manifest is a few hundred bytes, and the file comes from a folder anyone can write.
constexpr std::uintmax_t kMaxManifestBytes = 1024 * 1024;
// The longest free-text member (name, license, source) accepted.
constexpr std::size_t kMaxTextChars = 256;

bool refuse(std::string* why, std::string reason) {
    if (why) *why = "pack.json: " + std::move(reason);
    return false;
}

// The deepest nesting of arrays and objects a manifest may have. The deepest real member is two levels down; the
// limit exists because reflect-cpp's generic parser recurses once per level, and a deep document would overflow the
// stack of a host that reads it at launch.
constexpr int kMaxNesting = 32;

// `text` as a message may echo it: non-printable bytes escaped as \xNN and cut to `cap` bytes (never inside a UTF-8
// sequence), so a hostile manifest cannot put control codes or a megabyte into a log line. A value from the manifest
// is held to 64 bytes; the text of a library's own error, which names the cause at its end, to 256.
std::string echoed(std::string_view text, std::size_t cap = 64) {
    bool cut = false;
    if (text.size() > cap) {
        std::size_t at = cap;
        while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) --at;
        text = text.substr(0, at);
        cut = true;
    }
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f || c == '\\' || c == '"') {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
        } else {
            out += ch;
        }
    }
    if (cut) out += "...";
    return out;
}

// What DuplicateKeyScan found. Anything else (valid JSON with neither, or text it cannot follow, which the parse then
// reports) is Nothing.
struct ScanResult {
    enum class Kind { Nothing, Duplicate, TooDeep } kind = Kind::Nothing;
    std::string key;  // the repeated key, for Duplicate
};

// Scans `json` for the two things refused before it is parsed: nesting past kMaxNesting levels, and a key an object
// repeats at any depth. yyjson (under reflect-cpp) keeps the first of a repeated key and JS's JSON.parse the last, so
// a manifest the two readers would see differently is refused rather than parsed either way. The scan recurses at
// most kMaxNesting levels itself, and stops at the first finding.
class DuplicateKeyScan {
public:
    explicit DuplicateKeyScan(std::string_view s) : s_(s) {}

    ScanResult find() {
        value(0);
        return result_;
    }

private:

    void skipSpace() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\n' || s_[at_] == '\r')) ++at_;
    }

    // A string at at_ (on its opening quote), its escapes decoded to bytes; \u escapes become UTF-8.
    bool string(std::string& out) {
        if (at_ >= s_.size() || s_[at_] != '"') return false;
        ++at_;
        while (at_ < s_.size() && s_[at_] != '"') {
            char c = s_[at_++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= s_.size()) return false;
            c = s_[at_++];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (at_ + 4 > s_.size()) return false;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s_[at_++];
                        const int digit = h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0';
                        cp = cp * 16 + static_cast<unsigned>(digit);
                    }
                    // A surrogate half is kept as its own three bytes: two keys differ unless they spell it alike.
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xc0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3f));
                    } else {
                        out += static_cast<char>(0xe0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (cp & 0x3f));
                    }
                    break;
                }
                default: out += c; break;  // " \\ /
            }
        }
        if (at_ >= s_.size()) return false;
        ++at_;
        return true;
    }

    // One value at at_; false once the text stops being JSON this scan can follow, or a duplicate is found.
    bool value(int depth) {
        skipSpace();
        if (at_ >= s_.size()) return false;
        const char c = s_[at_];
        if (c == '{' || c == '[') {
            if (depth + 1 > kMaxNesting) {
                result_.kind = ScanResult::Kind::TooDeep;
                return false;
            }
            return c == '{' ? object(depth + 1) : array(depth + 1);
        }
        if (c == '"') {
            std::string ignored;
            return string(ignored);
        }
        while (at_ < s_.size() && s_[at_] != ',' && s_[at_] != '}' && s_[at_] != ']') ++at_;
        return true;
    }

    bool array(int depth) {
        ++at_;
        skipSpace();
        if (at_ < s_.size() && s_[at_] == ']') return ++at_, true;
        for (;;) {
            if (!value(depth)) return false;
            skipSpace();
            if (at_ >= s_.size()) return false;
            if (s_[at_] == ']') return ++at_, true;
            if (s_[at_++] != ',') return false;
        }
    }

    bool object(int depth) {
        ++at_;
        std::unordered_set<std::string> keys;
        skipSpace();
        if (at_ < s_.size() && s_[at_] == '}') return ++at_, true;
        for (;;) {
            skipSpace();
            std::string key;
            if (!string(key)) return false;
            if (!keys.insert(key).second) {
                result_ = {ScanResult::Kind::Duplicate, std::move(key)};
                return false;
            }
            skipSpace();
            if (at_ >= s_.size() || s_[at_++] != ':') return false;
            if (!value(depth)) return false;
            skipSpace();
            if (at_ >= s_.size()) return false;
            if (s_[at_] == '}') return ++at_, true;
            if (s_[at_++] != ',') return false;
        }
    }

    std::string_view           s_;
    std::size_t                at_ = 0;
    ScanResult                 result_;
};

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

// A segment Windows cannot hold as written: a reserved device name (CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM0 to COM9,
// LPT0 to LPT9, alone or before an extension, in any case), or one ending in a dot or a space, which Windows strips,
// so two spellings would open one file; or one holding a character Windows refuses in any name.
bool isWindowsReserved(std::string_view seg) {
    if (seg.back() == '.' || seg.back() == ' ') return true;
    // Characters Windows refuses in any file name (':' and '\\' are refused for the whole path already).
    if (seg.find_first_of("<>\"|?*") != std::string_view::npos) return true;
    std::string stem(seg.substr(0, seg.find('.')));
    // Windows ignores spaces before the extension too, so "con .txt" is the console.
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (char& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$")
        return true;
    return stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' &&
           stem[3] <= '9';
}

// A half's path: relative, inside the version folder, '/'-separated, printable ASCII (a path is opened as given on
// every platform, and Windows reads a narrow path in its ANSI code page), no empty, "." or ".." segment, and no segment
// Windows cannot hold.
bool isInsidePath(std::string_view p) {
    if (p.empty() || p.front() == '/') return false;
    for (const char c : p)
        if (c < ' ' || c > '~') return false;
    if (p.find('\\') != std::string_view::npos || p.find(':') != std::string_view::npos) return false;
    std::size_t at = 0;
    while (at <= p.size()) {
        std::size_t end = p.find('/', at);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view seg = p.substr(at, end - at);
        if (seg.empty() || seg == "." || seg == ".." || isWindowsReserved(seg)) return false;
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

// A free-text member: printable characters only (no control code, NUL included) and a bounded length.
bool checkText(const std::string& text, const char* member, std::string* why) {
    std::size_t chars = 0;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) return refuse(why, std::string(member) + " holds a control character");
        if ((c & 0xc0) != 0x80) ++chars;  // count code points, not UTF-8 continuation bytes
    }
    if (chars > kMaxTextChars)
        return refuse(why, std::string(member) + " is longer than " + std::to_string(kMaxTextChars) + " characters");
    return true;
}

bool checkList(const std::optional<std::vector<std::string>>& list, const char* member, bool packIds,
               std::vector<std::string>& out, std::string* why) {
    if (!list) return true;
    for (const auto& item : *list) {
        if (packIds ? !isPackId(item) : !isPlainName(item))
            return refuse(why, std::string(member) + " holds \"" + echoed(item) + "\", which is not " +
                                   (packIds ? "a pack id" : "an id"));
        for (const auto& seen : out)
            if (seen == item) return refuse(why, std::string(member) + " names \"" + echoed(item) + "\" twice");
        out.push_back(item);
    }
    return true;
}

}  // namespace

const std::vector<std::string>& knownHalves() {
    static const std::vector<std::string> kHalves = {"native", "app", "consoles", "protocol",
                                                     "ui",     "views", "cli",    "script", "placer", "kinds"};
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

std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why,
                                              std::optional<std::uint32_t> hostLevel) {
    const ScanResult scan = DuplicateKeyScan(json).find();
    if (scan.kind == ScanResult::Kind::TooDeep) {
        refuse(why, "nested deeper than " + std::to_string(kMaxNesting) + " levels");
        return std::nullopt;
    }
    if (scan.kind == ScanResult::Kind::Duplicate) {
        refuse(why, "the member \"" + echoed(scan.key) + "\" appears twice in one object");
        return std::nullopt;
    }
    if (hostLevel) {
        const auto peek = rfl::json::read<LevelPeek>(std::string(json));
        if (peek && peek->id && isPackId(*peek->id) && peek->apiLevel && *peek->apiLevel >= 0 &&
            *peek->apiLevel <= 0xffffffffll && *peek->apiLevel != static_cast<std::int64_t>(*hostLevel)) {
            PackManifest m;
            m.id = *peek->id;
            m.apiLevel = static_cast<std::uint32_t>(*peek->apiLevel);
            return m;
        }
    }
    auto raw = rfl::json::read<RawManifest>(std::string(json));
    if (!raw) {
        refuse(why, std::string("not a manifest: ") + echoed(raw.error().what(), 256));
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
    if (!checkText(m.name, "name", why) || !checkText(m.license, "license", why) ||
        !checkText(m.source, "source", why))
        return std::nullopt;
    if (!isPackId(m.id)) {
        refuse(why, "id \"" + echoed(m.id) +
                        "\" is not a pack id (lowercase letters, digits and '-', starting with a letter)");
        return std::nullopt;
    }
    if (!isPackVersion(m.version)) {
        refuse(why, "version \"" + echoed(m.version) + "\" is not MAJOR.MINOR.PATCH");
        return std::nullopt;
    }
    if (!raw->apiLevel) {
        refuse(why, "apiLevel is required (a whole number)");
        return std::nullopt;
    }
    if (*raw->apiLevel < 0 || *raw->apiLevel > 0xffffffffll) {
        refuse(why, "apiLevel is " + std::to_string(*raw->apiLevel) + ", outside 0..4294967295");
        return std::nullopt;
    }
    m.apiLevel = static_cast<std::uint32_t>(*raw->apiLevel);
    if (const auto& r = raw->requirements.value()) {
        if (const auto& extra = r->extra; !extra.empty()) {
            refuse(why, "requires holds \"" + echoed(extra.begin()->first) + "\", which is no member of it");
            return std::nullopt;
        }
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
        if (const auto& extra = p->extra; !extra.empty()) {
            refuse(why, "provides holds \"" + echoed(extra.begin()->first) + "\", which is no member of it");
            return std::nullopt;
        }
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
                refuse(why, "halves." + echoed(kind) + " is no half this RetroPlug loads");
                return std::nullopt;
            }
            if (kind == "native" && value != "rp-" + m.id) {
                refuse(why, "halves.native is \"" + echoed(value) +
                                "\", and a pack's library is named after its id: rp-" +
                                m.id);
                return std::nullopt;
            }
            if (kind != "native" && !isInsidePath(value)) {
                refuse(why, "halves." + echoed(kind) + " is \"" + echoed(value) +
                                "\", which is not a path inside the pack");
                return std::nullopt;
            }
        }
        m.halves = *raw->halves;
    }
    // The largest heap a script half may declare: the box has 264 KiB of RAM in all, so no heap that fits it is larger
    // (BlipToaster's is 24 KiB), and the desktop's context (desktopContextBytes: four times it at 64 bits) stays small.
    constexpr std::int64_t kMaxScriptHeap = std::int64_t{256} << 10;
    const bool scriptHalf = m.halves.count("script") > 0;
    if (const auto& s = raw->script) {
        if (!scriptHalf) {
            refuse(why, "script is set, and halves names no script half");
            return std::nullopt;
        }
        if (const auto& extra = s->extra; !extra.empty()) {
            refuse(why, "script holds \"" + echoed(extra.begin()->first) + "\", which is no member of it");
            return std::nullopt;
        }
        ScriptSpec spec;
        if (!s->runtime || s->runtime->empty()) {
            refuse(why, "script.runtime is required (the device runtime's file)");
            return std::nullopt;
        }
        if (!isInsidePath(*s->runtime)) {
            refuse(why, "script.runtime is \"" + echoed(*s->runtime) + "\", which is not a path inside the pack");
            return std::nullopt;
        }
        spec.runtime = *s->runtime;
        if (!s->heapBytes) {
            refuse(why, "script.heapBytes is required (the box's context, in bytes)");
            return std::nullopt;
        }
        if (*s->heapBytes < 1 || *s->heapBytes > kMaxScriptHeap) {
            refuse(why, "script.heapBytes is " + std::to_string(*s->heapBytes) + ", outside 1.." +
                            std::to_string(kMaxScriptHeap));
            return std::nullopt;
        }
        spec.heapBytes = static_cast<std::uint32_t>(*s->heapBytes);
        if (!checkList(s->nodeTypes, "script.nodeTypes", false, spec.nodeTypes, why)) return std::nullopt;
        if (spec.nodeTypes.empty()) {
            refuse(why, "script.nodeTypes is empty: a script half registers node types");
            return std::nullopt;
        }
        for (const auto& t : spec.nodeTypes) {
            if (std::find(m.providesNodeTypes.begin(), m.providesNodeTypes.end(), t) == m.providesNodeTypes.end()) {
                refuse(why, "script.nodeTypes names " + echoed(t) + ", which provides.nodeTypes does not declare");
                return std::nullopt;
            }
        }
        m.script = std::move(spec);
    } else if (scriptHalf) {
        refuse(why, "halves.script needs the script member (its runtime, heapBytes and nodeTypes)");
        return std::nullopt;
    }
    const bool protocolHalf = m.halves.count("protocol") > 0;
    if (const auto& p = raw->protocol) {
        if (!protocolHalf) {
            refuse(why, "protocol is set, and halves names no protocol half");
            return std::nullopt;
        }
        if (const auto& extra = p->extra; !extra.empty()) {
            const std::string& key = extra.begin()->first;
            refuse(why, key == "kinds" ? std::string("protocol.kinds: a pack's kind items are its top-level kinds member")
                                       : "protocol holds \"" + echoed(key) + "\", which is no member of it");
            return std::nullopt;
        }
        ProtocolSpec spec;
        if (!p->nodes || p->nodes->empty()) {
            refuse(why, "protocol.nodes is empty: a protocol half registers node types");
            return std::nullopt;
        }
        const std::vector<rfl::Generic>& nodes = *p->nodes;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto node = rfl::from_generic<RawProtocolNode>(nodes[i]);
            if (!node || !node->type || node->type->empty()) {
                refuse(why, "protocol.nodes[" + std::to_string(i) + "] has no \"type\"");
                return std::nullopt;
            }
            const std::string& type = *node->type;
            if (node->extra.count("label") > 0) {
                refuse(why, "protocol.nodes gives " + echoed(type) +
                                " a label: a pack's script nodes take their span labels at load");
                return std::nullopt;
            }
            if (std::find(spec.nodeTypes.begin(), spec.nodeTypes.end(), type) != spec.nodeTypes.end()) {
                refuse(why, "protocol.nodes names " + echoed(type) + " twice");
                return std::nullopt;
            }
            if (std::find(m.providesNodeTypes.begin(), m.providesNodeTypes.end(), type) == m.providesNodeTypes.end()) {
                refuse(why, "protocol.nodes names " + echoed(type) + ", which provides.nodeTypes does not declare");
                return std::nullopt;
            }
            if (m.script && std::find(m.script->nodeTypes.begin(), m.script->nodeTypes.end(), type) !=
                                m.script->nodeTypes.end()) {
                refuse(why, "protocol.nodes names " + echoed(type) + ", which script.nodeTypes names too");
                return std::nullopt;
            }
            spec.nodeTypes.push_back(type);
        }
        for (const auto& [kind, type] : p->roles.value_or(std::map<std::string, std::string>{})) {
            if (std::find(m.providesRoles.begin(), m.providesRoles.end(), kind) == m.providesRoles.end()) {
                refuse(why, "protocol.roles names " + echoed(kind) + ", which provides.roles does not declare");
                return std::nullopt;
            }
            if (std::find(spec.nodeTypes.begin(), spec.nodeTypes.end(), type) == spec.nodeTypes.end()) {
                refuse(why, "protocol.roles places " + echoed(kind) + " as " + echoed(type) +
                                ", which protocol.nodes does not declare");
                return std::nullopt;
            }
            spec.roles.emplace(kind, type);
        }
        // The nodes are re-read with parseJson, which keeps an integer above INT64_MAX as JS would (reflect-cpp's reader
        // would wrap it negative); the scan above has already refused duplicate keys and deep nesting.
        const auto whole = rp::graph::parseJson(json);
        const auto top = whole ? whole->to_object() : rfl::Result<rfl::Generic::Object>(whole.error());
        const auto proto = top ? top->get("protocol") : rfl::Result<rfl::Generic>(top.error());
        const auto protoObj = proto ? proto->to_object() : rfl::Result<rfl::Generic::Object>(proto.error());
        const auto arr = protoObj ? protoObj->get("nodes") : rfl::Result<rfl::Generic>(protoObj.error());
        if (!arr || !arr->to_array()) {
            refuse(why, "protocol.nodes is not a list of nodes");
            return std::nullopt;
        }
        spec.nodesJson = rp::graph::canonicalJson(*arr);
        m.protocol = std::move(spec);
    } else if (protocolHalf) {
        refuse(why, "halves.protocol needs the protocol member (its nodes and roles)");
        return std::nullopt;
    }
    const bool kindsHalf = m.halves.count("kinds") > 0;
    if (const auto& k = raw->kinds) {
        if (!kindsHalf) {
            refuse(why, "kinds is set, and halves names no kinds half");
            return std::nullopt;
        }
        if (k->empty()) {
            refuse(why, "kinds names no kind: a kinds half registers items");
            return std::nullopt;
        }
        KindsSpec spec;
        for (const auto& [kind, items] : *k) {
            if (items.empty()) {
                refuse(why, "kinds." + echoed(kind) + " is empty");
                return std::nullopt;
            }
            std::vector<std::string>& ids = spec.ids[kind];
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto item = rfl::from_generic<RawKindItem>(items[i]);
                if (!item) {
                    refuse(why, "kinds." + echoed(kind) + "[" + std::to_string(i) + "] is not an object");
                    return std::nullopt;
                }
                if (!item->id || item->id->empty()) {
                    refuse(why, "kinds." + echoed(kind) + "[" + std::to_string(i) + "] has no \"id\"");
                    return std::nullopt;
                }
                if (std::find(ids.begin(), ids.end(), *item->id) != ids.end()) {
                    refuse(why, "kinds." + echoed(kind) + " names " + echoed(*item->id) + " twice");
                    return std::nullopt;
                }
                ids.push_back(*item->id);
            }
        }
        // Re-read with parseJson, as the protocol nodes are, so an integer above INT64_MAX stays a number.
        const auto whole = rp::graph::parseJson(json);
        const auto top = whole ? whole->to_object() : rfl::Result<rfl::Generic::Object>(whole.error());
        const auto member = top ? top->get("kinds") : rfl::Result<rfl::Generic>(top.error());
        if (!member || !member->to_object()) {
            refuse(why, "kinds is not a map of kind to items");
            return std::nullopt;
        }
        spec.kindsJson = rp::graph::canonicalJson(*member);
        m.kinds = std::move(spec);
    } else if (kindsHalf) {
        refuse(why, "halves.kinds needs the kinds member (each kind's items)");
        return std::nullopt;
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

std::optional<PackManifest> readPackManifest(const fs::path& versionDir, std::string* why,
                                             std::optional<std::uint32_t> hostLevel) {
    const fs::path file = versionDir / "pack.json";
    std::error_code ec;
    const auto size = fs::is_regular_file(file, ec) ? fs::file_size(file, ec) : 0;
    if (ec || !fs::is_regular_file(file, ec)) {
        if (why) *why = "pack.json: cannot read " + displayPath(file);
        return std::nullopt;
    }
    if (size > kMaxManifestBytes) {
        if (why) *why = "pack.json: too large (over " + std::to_string(kMaxManifestBytes) + " bytes)";
        return std::nullopt;
    }
    // One byte past the size taken is read, so a file that grew since is refused, not truncated and parsed.
    std::ifstream in(file, std::ios::binary);
    std::string text(static_cast<std::size_t>(std::min<std::uintmax_t>(size, kMaxManifestBytes)) + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.bad() || (in.fail() && !in.eof())) {
        if (why) *why = "pack.json: cannot read " + displayPath(file);
        return std::nullopt;
    }
    const bool full = static_cast<std::size_t>(in.gcount()) == text.size();
    const bool grew = full && in.peek() != std::char_traits<char>::eof();
    text.resize(static_cast<std::size_t>(in.gcount()));
    if (grew && text.size() <= kMaxManifestBytes) {
        if (why) *why = "pack.json: it changed while it was read";
        return std::nullopt;
    }
    if (text.size() > kMaxManifestBytes) {
        if (why) *why = "pack.json: too large (over " + std::to_string(kMaxManifestBytes) + " bytes)";
        return std::nullopt;
    }
    return parsePackManifest(text, why, hostLevel);
}

}  // namespace rp::host::packs
