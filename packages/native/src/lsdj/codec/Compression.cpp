#include "lsdj/codec/Compression.hpp"

#include <cstring>

namespace rp::lsdj::codec {
namespace {

constexpr std::size_t  kSongBytes = 0x8000;
constexpr std::uint8_t RLE = 0xC0;     // RUN_LENGTH_ENCODING_BYTE
constexpr std::uint8_t SA  = 0xE0;     // SPECIAL_ACTION_BYTE
constexpr std::uint8_t DEFAULT_WAVE_BYTE = 0xF0;
constexpr std::uint8_t DEFAULT_INSTR_BYTE = 0xF1;

constexpr std::uint8_t DEFAULT_WAVE[16] = {
    0x8E, 0xCD, 0xCC, 0xBB, 0xAA, 0xA9, 0x99, 0x88,
    0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31};
constexpr std::uint8_t DEFAULT_INSTRUMENT[16] = {
    0xA8, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00,
    0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00, 0x00};

} // namespace

rfl::Result<std::vector<std::uint8_t>>
decompressProject(std::span<const std::uint8_t> blockArea, std::uint8_t startBlock) {
    std::vector<std::uint8_t> out;
    out.reserve(kSongBytes);

    std::size_t curBlock = startBlock;
    auto rd = [&](std::size_t& pos) -> rfl::Result<std::uint8_t> {
        if (pos >= blockArea.size()) return rfl::error("decompress read past end of block area");
        return blockArea[pos++];
    };

    for (int guard = 0; guard <= static_cast<int>(kBlockCount); ++guard) {
        std::size_t pos = curBlock * kBlockSize;
        int nextJump = -1; // set when a jump/EOF step is read
        while (nextJump < 0) {
            auto bres = rd(pos);
            if (!bres) return rfl::error(bres.error().what());
            const std::uint8_t byte = bres.value();
            if (byte == RLE) {
                auto b = rd(pos); if (!b) return rfl::error(b.error().what());
                if (b.value() == RLE) {
                    out.push_back(RLE);
                } else {
                    auto c = rd(pos); if (!c) return rfl::error(c.error().what());
                    out.insert(out.end(), c.value(), b.value());
                }
            } else if (byte == SA) {
                auto b = rd(pos); if (!b) return rfl::error(b.error().what());
                const std::uint8_t a = b.value();
                if (a == SA) {
                    out.push_back(SA);
                } else if (a == DEFAULT_WAVE_BYTE) {
                    auto c = rd(pos); if (!c) return rfl::error(c.error().what());
                    for (std::uint8_t k = 0; k < c.value(); ++k)
                        out.insert(out.end(), DEFAULT_WAVE, DEFAULT_WAVE + 16);
                } else if (a == DEFAULT_INSTR_BYTE) {
                    auto c = rd(pos); if (!c) return rfl::error(c.error().what());
                    for (std::uint8_t k = 0; k < c.value(); ++k)
                        out.insert(out.end(), DEFAULT_INSTRUMENT, DEFAULT_INSTRUMENT + 16);
                } else {
                    nextJump = a; // block jump or EOF
                }
            } else {
                out.push_back(byte);
            }
            if (out.size() > kSongBytes) return rfl::error("decompress overflowed 0x8000 bytes");
        }
        if (static_cast<std::uint8_t>(nextJump) == kEofBlock) break;
        const std::size_t target = static_cast<std::size_t>(nextJump) - 1; // 1-based -> 0-based
        if (target >= kBlockCount) return rfl::error("decompress jump out of range");
        curBlock = target;
    }

    if (out.size() != kSongBytes) return rfl::error("decompressed song is not 0x8000 bytes");
    return out;
}

rfl::Result<Compressed> compressProject(std::span<const std::uint8_t> song, std::uint8_t startBlock) {
    if (song.size() < kSongBytes) return rfl::error("song smaller than 0x8000 bytes");

    Compressed result;
    std::vector<std::uint8_t>& out = result.bytes;

    unsigned currentBlock = startBlock; // 1-based
    std::size_t currentBlockSize = 0;
    std::size_t read = 0;

    auto matchesRun = [&](const std::uint8_t* pat) {
        return read + 16 < kSongBytes && std::memcmp(&song[read], pat, 16) == 0;
    };

    while (read < kSongBytes) {
        std::uint8_t event[3] = {0, 0, 0};
        std::size_t eventSize = 0;

        std::uint8_t dwCount = 0;
        while (matchesRun(DEFAULT_WAVE) && dwCount != 0xFF) { read += 16; ++dwCount; }
        if (dwCount > 0) {
            event[0] = SA; event[1] = DEFAULT_WAVE_BYTE; event[2] = dwCount; eventSize = 3;
        } else {
            std::uint8_t diCount = 0;
            while (matchesRun(DEFAULT_INSTRUMENT) && diCount != 0xFF) { read += 16; ++diCount; }
            if (diCount > 0) {
                event[0] = SA; event[1] = DEFAULT_INSTR_BYTE; event[2] = diCount; eventSize = 3;
            } else {
                const std::uint8_t c = song[read];
                if (c == RLE) {
                    event[0] = RLE; event[1] = RLE; eventSize = 2; ++read;
                } else if (c == SA) {
                    event[0] = SA; event[1] = SA; eventSize = 2; ++read;
                } else if (read + 3 < kSongBytes && song[read + 1] == c &&
                           song[read + 2] == c && song[read + 3] == c) {
                    std::uint8_t count = 0;
                    while (read < kSongBytes && song[read] == c && count != 0xFF) { ++count; ++read; }
                    event[0] = RLE; event[1] = c; event[2] = count; eventSize = 3;
                } else {
                    event[0] = song[read++]; eventSize = 1;
                }
            }
        }

        if (currentBlockSize + eventSize + 2 >= kBlockSize) {
            out.push_back(SA);
            out.push_back(static_cast<std::uint8_t>(currentBlock + 1));
            currentBlockSize += 2;
            while (currentBlockSize < kBlockSize) { out.push_back(0); ++currentBlockSize; }
            ++currentBlock;
            currentBlockSize = 0;
            if (currentBlock == kBlockCount + 1) return rfl::error("compressed song exceeds block count");
            // fall through: write the event in the new block
        }

        out.insert(out.end(), event, event + eventSize);
        currentBlockSize += eventSize;
    }

    out.push_back(SA);
    out.push_back(kEofBlock);
    if (currentBlockSize > 0) {
        currentBlockSize += 2;
        while (currentBlockSize < kBlockSize) { out.push_back(0); ++currentBlockSize; }
    }

    result.blockCount = out.size() / kBlockSize;
    return result;
}

} // namespace rp::lsdj::codec
