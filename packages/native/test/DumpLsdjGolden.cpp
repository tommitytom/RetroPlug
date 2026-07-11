// Golden-vector dumper for the LSDj sav codec. For each input .sav it writes the
// C++ reference decode as JSON (savToJson ∘ decodeSav) next to it. The pure-TS
// port's decode is asserted byte-for-byte against these frozen models — the C++
// codec is the reference (validated by retroplug-lsdj-diff-tests vs liblsdj for
// fmt<=16, and by retroplug-sav-tests byte-identity round-trip for all formats).
//
//   retroplug-lsdj-golden-dump <outDir> <in1.sav> [in2.sav ...]
//
// Writes <outDir>/<stem>.json per input. Exit non-zero if any decode fails.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include "lsdj/SavSerialization.hpp"
#include "lsdj/codec/SavCodec.hpp"

namespace fs = std::filesystem;
using namespace rp::lsdj;

static std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <outDir> <in.sav> [more.sav ...]\n", argv[0]);
        return 2;
    }
    const fs::path outDir = argv[1];
    fs::create_directories(outDir);

    int failures = 0;
    for (int i = 2; i < argc; ++i) {
        const fs::path in = argv[i];
        const auto bytes = slurp(in);
        if (bytes.size() < 0x8000) {
            std::fprintf(stderr, "SKIP %s: %zu bytes (< 0x8000)\n", in.string().c_str(), bytes.size());
            continue;
        }
        auto sav = codec::decodeSav(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
        if (!sav) {
            std::fprintf(stderr, "FAIL %s: %s\n", in.string().c_str(), sav.error().what().c_str());
            ++failures;
            continue;
        }
        const std::string json = savToJson(sav.value());
        const fs::path out = outDir / (in.stem().string() + ".json");
        std::ofstream(out, std::ios::binary).write(json.data(), static_cast<std::streamsize>(json.size()));
        std::fprintf(stderr, "OK   %s -> %s (%zu bytes json)\n",
                     in.filename().string().c_str(), out.filename().string().c_str(), json.size());
    }
    return failures ? 1 : 0;
}
