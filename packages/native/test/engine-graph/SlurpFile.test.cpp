// The reader EngineRpcService's putResource uses for a `path` resource (docs/modular-graph-phase-c3-design.md
// section 4): a file is read WHOLE, by the size it reports, so a read that stops short answers no value rather than
// the short or empty vector a copy run to the end would hand back as a blank battery for the core to auto-save over.
// What is constructible without privileges is the family of paths that open and will not read: one naming a directory,
// which on Linux opens and fails every read of it (a genuine I/O error needs a failing device or root, so that is the
// short read this can exercise). What the other half turns on is a file that opens and holds nothing: a value of zero
// bytes, which is a blank battery rather than a failure.
//
// Run via `pnpm test:plugin engine-graph`.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "util/SlurpFile.hpp"

namespace {

// A file of `n` bytes, each one derivable from its offset so a payload that came back shifted, short or reordered
// shows as a mismatch rather than as a size that happens to fit.
std::vector<std::uint8_t> pattern(std::size_t n) {
    std::vector<std::uint8_t> bytes(n);
    for (std::size_t i = 0; i < n; ++i) bytes[i] = static_cast<std::uint8_t>(i * 31U + 7U);
    return bytes;
}

std::string stage(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!bytes.empty())
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
    return path.generic_string();
}

} // namespace

TEST_CASE("slurpAll reads a file whole, and answers nothing when it cannot read it", "[engine-graph][resource]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("rp-slurp-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);

    // A file that is not there: the open answers it.
    CHECK_FALSE(slurpAll((dir / "absent.sav").generic_string()));

    // A directory: the open succeeds, the read is what fails, and a battery named by such a path must be refused
    // rather than read as nothing at all. (On a platform whose open refuses a directory, the open answers instead.)
    CHECK_FALSE(slurpAll(dir.generic_string()));

    // An empty file is not a failure: it is the empty payload a zero-byte byte block already was.
    const std::string emptyPath = stage(dir / "empty.sav", {});
    const auto        empty = slurpAll(emptyPath);
    REQUIRE(empty.has_value());
    CHECK(empty->empty());

    // A whole file, every byte in order. One of exactly the chunk the reader asks for, and one of several, so the
    // bytes must add up to the size the stream reported instead of stopping at the first read that came back clean.
    for (const std::size_t size : {std::size_t{65536}, std::size_t{200000}}) {
        const std::string     path = stage(dir / ("payload-" + std::to_string(size) + ".sav"), pattern(size));
        const auto            got = slurpAll(path);
        REQUIRE(got.has_value());
        CHECK(*got == pattern(size));
    }

    std::filesystem::remove_all(dir);
}
