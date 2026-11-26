// The crossing rules of the pinned C++ boundary (docs/downloadable-packs-loading-design.md section 2) that a test can
// hold: no RTTI cast, typeid, any_cast or type_index in the code that handles a pack's objects. A pack's types (a
// console's SystemBase subclass, its capability interfaces) carry their own typeinfo, local to its module, so a
// dynamic_cast, dynamic_pointer_cast or typeid on one of them depends on how the platform compares typeinfo across
// modules; the host calls them through virtual functions and branches on null interface pointers instead. Any built-in
// package may hold a pack's object (the engine, the system contract, the graph runtime, a host, a cart package beside
// a console pack), so the scan is every package's native source and the shell's own source and hosts, but for Mesen's,
// which becomes a pack and may cast its own objects inside itself. Tests and vendored dependencies are not scanned: a
// test may cast the cores it built.
//
// And no heap ownership crosses (docs/downloadable-packs-m5a-plan.md): the contract headers' public virtuals return no
// owning type and take none but by const reference, and the build path's types hold none;
// `retroplug-packs-heap-test` proves it at run time.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-boundary]"`.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "system/DebugTarget.hpp"
#include "system/Sink.hpp"
#include "system/SystemBase.hpp"

namespace fs = std::filesystem;

namespace {

// The folders scanned: every packages/<name>/native/src but Mesen's, the shell's src, and its hosts.
std::vector<fs::path> scannedRoots(const fs::path& repo) {
    std::vector<fs::path> roots;
    for (const auto& package : fs::directory_iterator(repo / "packages")) {
        const fs::path src = package.path() / "native" / "src";
        if (package.path().filename() != "mesen" && fs::is_directory(src)) roots.push_back(src);
    }
    roots.push_back(repo / "packages" / "native" / "src");  // the shell's own: Engine, the system table, the packs
    for (const char* host : {"plugin", "cli", "sdl", "node"})
        if (fs::is_directory(repo / "packages" / "native" / host)) roots.push_back(repo / "packages" / "native" / host);
    return roots;
}

// What reads a type's identity across modules: the casts, typeid (with or without a space), std::any_cast (which
// compares typeinfo) and std::type_index.
const std::regex kRttiUse(R"(dynamic_cast|dynamic_pointer_cast|typeid\s*\(|any_cast|type_index)");

}  // namespace

TEST_CASE("the host's code casts no object by RTTI", "[packs-boundary]") {
    const fs::path repo(RP_REPO_ROOT);
    const std::vector<fs::path> roots = scannedRoots(repo);
    REQUIRE(roots.size() > 10);
    std::vector<std::string> found;
    std::size_t files = 0;
    for (const fs::path& root : roots) {
        for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
            const std::string name = it->path().filename().string();
            if (it->is_directory() && (name == "node_modules" || name == "test")) {
                it.disable_recursion_pending();
                continue;
            }
            const std::string ext = it->path().extension().string();
            if (!it->is_regular_file() || (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".c" &&
                                           ext != ".mm"))
                continue;
            ++files;
            std::ifstream in(it->path(), std::ios::binary);
            std::string line;
            for (int n = 1; std::getline(in, line); ++n) {
                // What a line says outside its comment: the code, not a note about why there is no cast.
                const std::string code = line.substr(0, line.find("//"));
                if (std::regex_search(code, kRttiUse))
                    found.push_back(it->path().string() + ":" + std::to_string(n) + ": " + line);
            }
        }
    }
    CHECK(files > 150);  // the scan read the trees, not an empty folder
    for (const auto& f : found) UNSCOPED_INFO(f);
    CHECK(found.empty());
}

namespace {

// The contract headers whose classes a pack's systems implement and the host calls.
const char* const kContractHeaders[] = {"SystemBase.hpp", "SystemCapabilities.hpp", "DebugTarget.hpp",
                                        "ChannelTap.hpp"};
// The build path's types, which a pack's node fills and the host passes back to the pack's build function.
const char* const kBuildHeaders[] = {"SystemBuild.hpp", "HostSystems.hpp"};

// A type that owns heap memory: the standard containers, strings (not string_view), the owning pointers and
// functions, and reflect-cpp's types (rfl::Bytestring is a vector).
const std::regex kOwning(
    R"(std::(vector|string\b|basic_string|map|unordered_map|set|unordered_set|list|deque|function|unique_ptr|shared_ptr)|rfl::)");

// The plain types a public virtual may return besides a pointer (a borrowed one, the callee's for its own life). A type
// joins this list only if it holds no heap memory; the structs among them are held trivially copyable below.
const std::set<std::string> kPlainReturns = {
    "void", "bool", "float", "double", "std::size_t", "std::uint8_t", "std::uint16_t", "std::uint32_t",
    "std::uint64_t", "std::int32_t", "std::optional<std::uint8_t>", "std::optional<std::uint32_t>",
    "std::string_view", "std::span<const std::uint8_t>", "std::span<const Region>", "CoreTransportStats",
    "rp::MemoryAccessor", "ApuState", "BreakInfo", "WriteResult"};
static_assert(std::is_trivially_copyable_v<CoreTransportStats>);
static_assert(std::is_trivially_copyable_v<rp::MemoryAccessor>);
static_assert(std::is_trivially_copyable_v<rp::ApuState>);
static_assert(std::is_trivially_copyable_v<rp::BreakInfo>);

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The code of a header: its // and /* */ comments blanked.
std::string withoutComments(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (text.compare(i, 2, "/*") == 0) {
            const auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            out.push_back(' ');
        } else {
            out.push_back(text[i++]);
        }
    }
    return out;
}

std::string trimmed(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// The access in effect at `at`: the last access label, or, where a class or struct opened after it, that one's default.
std::string accessAt(const std::string& code, std::size_t at) {
    static const std::regex kLabel(R"(\b(public|protected|private)\s*:)");
    static const std::regex kOpen(R"(\b(class|struct)\s+\w+[^;{()]*\{)");
    std::string access = "public";
    std::size_t last = 0;
    bool        any = false;
    const auto  end = code.cbegin() + static_cast<std::ptrdiff_t>(at);
    for (auto m = std::sregex_iterator(code.cbegin(), end, kLabel); m != std::sregex_iterator(); ++m) {
        last = static_cast<std::size_t>(m->position(0));
        any = true;
        access = (*m)[1].str();
    }
    for (auto m = std::sregex_iterator(code.cbegin(), end, kOpen); m != std::sregex_iterator(); ++m) {
        const auto pos = static_cast<std::size_t>(m->position(0));
        if (!any || pos >= last) {
            last = pos;
            any = true;
            access = (*m)[1].str() == "struct" ? "public" : "private";
        }
    }
    return access;
}

// A parameter list's parameters, split at the commas outside angle brackets, each without its default argument.
std::vector<std::string> parametersOf(const std::string& list) {
    std::vector<std::string> out;
    std::string              current;
    int                      depth = 0;
    for (const char c : list) {
        if (c == '<') ++depth;
        if (c == '>') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    for (auto& p : out) p = trimmed(p.substr(0, p.find('=')));
    std::erase_if(out, [](const std::string& p) { return p.empty() || p == "void"; });
    return out;
}

// Why a parameter crosses with ownership, or empty: an rvalue reference, or an owning type that is not a const
// reference. A sink's item type is lent by const reference by construction, so a Sink<...> is judged as the sink.
std::string parameterFault(std::string p) {
    if (p.find("&&") != std::string::npos) return "takes an rvalue reference";
    p = std::regex_replace(p, std::regex(R"((rp::)?Sink<[^>]*>)"), "Sink");
    if (!std::regex_search(p, kOwning)) return {};
    if (p.rfind("const ", 0) == 0 && p.find('&') != std::string::npos) return {};
    return "takes an owning type other than by const reference";
}

}  // namespace

TEST_CASE("a sink copies into the container of the side that made it", "[packs-boundary]") {
    std::vector<std::uint8_t> bytes;
    const rp::ByteSink        byteSink = rp::appendBytesTo(bytes);
    const std::uint8_t        a[] = {1, 2, 3};
    byteSink(a, 3);
    byteSink(a, 0);  // an empty write is nothing
    byteSink(a + 1, 2);
    CHECK(bytes == std::vector<std::uint8_t>{1, 2, 3, 2, 3});

    std::vector<std::string> items;
    const auto               itemSink = rp::appendTo(items);
    {
        const std::string lent = "a string lent for the call, past any short-string buffer";
        itemSink(lent);
    }  // the lender's copy is gone; the sink's is the caller's own
    REQUIRE(items.size() == 1);
    CHECK(items[0] == "a string lent for the call, past any short-string buffer");

    std::string one = "before";
    const auto  assign = rp::assignTo(one);
    assign(std::string("first"));
    assign(std::string("second"));
    CHECK(one == "second");

    const rp::ByteSink none{};
    none(a, 3);  // a sink with no function takes nothing and does not crash
}

TEST_CASE("the contract headers hand no heap ownership across a pack's boundary", "[packs-boundary]") {
    const fs::path dir = fs::path(RP_REPO_ROOT) / "packages" / "system" / "native" / "src" / "system";
    static const std::regex kVirtual(R"(virtual\s+([^;{}()]*?)\s*\b(\w+)\s*\(([^()]*)\))");
    std::vector<std::string> found;
    std::size_t              publicVirtuals = 0;
    for (const char* header : kContractHeaders) {
        const std::string code = withoutComments(readText(dir / header));
        REQUIRE_FALSE(code.empty());
        for (auto m = std::sregex_iterator(code.cbegin(), code.cend(), kVirtual); m != std::sregex_iterator(); ++m) {
            const std::string ret = trimmed((*m)[1].str());
            const std::string name = (*m)[2].str();
            if (ret.empty() || ret.back() == '~') continue;  // a destructor
            if (accessAt(code, static_cast<std::size_t>(m->position(0))) != "public") continue;
            ++publicVirtuals;
            const std::string where = std::string(header) + ": " + ret + " " + name + "(" + (*m)[3].str() + ")";
            if (ret.back() != '*' && !kPlainReturns.contains(ret)) found.push_back(where + ": returns " + ret);
            for (const auto& p : parametersOf((*m)[3].str()))
                if (const auto fault = parameterFault(p); !fault.empty()) found.push_back(where + ": " + p + " " + fault);
        }
    }
    for (const char* header : kBuildHeaders) {
        // The one object that crosses is a core, carried out of a build function in a unique_ptr and deleted through
        // its virtual destructor (the pack's deleter); nothing else in the build path may own memory.
        std::string code = withoutComments(readText(dir / header));
        REQUIRE_FALSE(code.empty());
        code = std::regex_replace(code, std::regex(R"(std::unique_ptr<SystemBase>)"), "CoreHandle");
        if (std::regex_search(code, kOwning)) found.push_back(std::string(header) + ": holds or passes an owning type");
        if (code.find("&&") != std::string::npos) found.push_back(std::string(header) + ": takes an rvalue reference");
        // The request itself crosses by value where it is named as a parameter of a function pointer.
        if (std::regex_search(code, std::regex(R"(\bSystemBuildRequest\s+\w)")))
            found.push_back(std::string(header) + ": passes a SystemBuildRequest by value");
    }
    CHECK(publicVirtuals >= 50);  // the scan read the interfaces, not an empty match
    UNSCOPED_INFO("public virtuals scanned: " << publicVirtuals);
    for (const auto& f : found) UNSCOPED_INFO(f);
    // Today's contract hands ownership across; docs/downloadable-packs-m5a-plan.md Task 5 makes this CHECK(found.empty()).
    CHECK_FALSE(found.empty());
}
