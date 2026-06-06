#include "lsdj/codec/SavCodec.hpp"

#include "lsdj/codec/Compression.hpp"
#include "lsdj/codec/SongCodec.hpp"

#include <cstring>

namespace rp::lsdj::codec {
namespace {

constexpr std::size_t kWorkingSong = 0;
constexpr std::size_t kSongBytes   = 0x8000;
constexpr std::size_t kHeader      = 0x8000;
constexpr std::size_t kProjectNames = 0x8000; // [32][8]
constexpr std::size_t kProjectVers  = 0x8100; // [32]
constexpr std::size_t kReserved     = 0x8120; // [30]
constexpr std::size_t kInit         = 0x813E; // 'jk'
constexpr std::size_t kActiveProj   = 0x8140;
constexpr std::size_t kAllocTable   = 0x8141; // [191]
constexpr std::size_t kBlockArea    = 0x8200; // 191 * 0x200
constexpr std::size_t kNameLen      = 8;
constexpr std::size_t kProjectCount = 32;

std::string readName(std::span<const std::uint8_t> b, std::size_t off) {
    std::string s;
    for (std::size_t i = 0; i < kNameLen; ++i) {
        const char c = static_cast<char>(b[off + i]);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

} // namespace

rfl::Result<model::Sav> decodeSav(std::span<const std::uint8_t> savBytes) {
    if (savBytes.size() < kSavSize) return rfl::error("sav smaller than 128 KiB");
    if (savBytes[kInit] != 'j' || savBytes[kInit + 1] != 'k')
        return rfl::error("missing 'jk' SRAM init magic");

    model::Sav sav;
    sav.activeProjectIndex = savBytes[kActiveProj];
    std::memcpy(sav.reserved.data(), savBytes.data() + kReserved, sav.reserved.size());

    auto ws = decodeSong(savBytes.subspan(kWorkingSong, kSongBytes));
    if (!ws) return rfl::error(ws.error().what());
    sav.workingSong = std::move(ws.value());

    const std::span<const std::uint8_t> blockArea = savBytes.subspan(kBlockArea, kBlockCount * kBlockSize);

    // Decompress each stored project: walk the block allocation table; the first
    // block carrying project index p is that project's entry point.
    for (std::size_t i = 0; i < kBlockCount; ++i) {
        const std::uint8_t p = savBytes[kAllocTable + i];
        if (p == kEmptyBlock || p >= kProjectCount) continue;
        if (sav.projects[p]) continue;

        auto songBytes = decompressProject(blockArea, static_cast<std::uint8_t>(i));
        if (!songBytes) return rfl::error(std::string("project ") + std::to_string(p) + ": " + songBytes.error().what());
        auto song = decodeSong(songBytes.value());
        if (!song) return rfl::error(song.error().what());

        model::StoredProject proj;
        proj.name    = readName(savBytes, kProjectNames + p * kNameLen);
        proj.version = savBytes[kProjectVers + p];
        proj.song    = std::move(song.value());
        sav.projects[p] = std::move(proj);
    }

    return sav;
}

std::vector<std::uint8_t> encodeSav(const model::Sav& sav, std::span<const std::uint8_t> templateBytes) {
    std::vector<std::uint8_t> out(kSavSize, 0);
    if (templateBytes.size() >= kSavSize)
        std::memcpy(out.data(), templateBytes.data(), kSavSize);

    // Working-memory song (pass the template's working song through so unmodeled
    // song regions stay byte-identical).
    {
        std::span<const std::uint8_t> tmpl =
            templateBytes.size() >= kSongBytes ? templateBytes.subspan(0, kSongBytes)
                                               : std::span<const std::uint8_t>{};
        const auto ws = encodeSong(sav.workingSong, tmpl);
        std::memcpy(out.data() + kWorkingSong, ws.data(), kSongBytes);
    }

    // Header. Name/version tables for ABSENT projects pass through from the
    // template (a fresh sav leaves version-dependent bytes there — 0 on 9.x,
    // 0xFF on some older builds — so zeroing them would break byte-identity);
    // only present projects are overwritten below. The block alloc table is
    // reset to the empty marker (matches a fresh corpus sav) then filled.
    std::memcpy(out.data() + kReserved, sav.reserved.data(), sav.reserved.size());
    out[kInit] = 'j';
    out[kInit + 1] = 'k';
    out[kActiveProj] = sav.activeProjectIndex;
    std::memset(out.data() + kAllocTable, kEmptyBlock, kBlockCount);

    // Compress present projects into the block area (sequential blocks, 1-based
    // like liblsdj compress_projects), filling names/versions and the alloc table.
    unsigned currentBlock = 1; // 1-based
    for (std::size_t i = 0; i < kProjectCount; ++i) {
        if (!sav.projects[i]) continue;
        const model::StoredProject& proj = *sav.projects[i];

        const std::string& name = proj.name;
        std::memcpy(out.data() + kProjectNames + i * kNameLen, name.data(),
                    std::min(name.size(), kNameLen));
        out[kProjectVers + i] = proj.version;

        const auto songBytes = encodeSong(proj.song);
        auto comp = compressProject(songBytes, static_cast<std::uint8_t>(currentBlock));
        if (!comp) continue; // song didn't fit; skip (won't happen for valid songs)

        const std::size_t dst = kBlockArea + (currentBlock - 1) * kBlockSize;
        std::memcpy(out.data() + dst, comp.value().bytes.data(), comp.value().bytes.size());
        for (std::size_t b = 0; b < comp.value().blockCount; ++b)
            out[kAllocTable + (currentBlock - 1) + b] = static_cast<std::uint8_t>(i);
        currentBlock += comp.value().blockCount;
    }

    return out;
}

} // namespace rp::lsdj::codec
