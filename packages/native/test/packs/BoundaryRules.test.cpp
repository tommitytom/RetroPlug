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
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-boundary]"`.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

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
