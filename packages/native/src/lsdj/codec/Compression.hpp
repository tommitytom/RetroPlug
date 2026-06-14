#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <rfl/Result.hpp>

// LSDj's RLE block compression for stored-project songs. Faithful port of
// liblsdj compression.c: tokens 0xC0 (RLE), 0xE0 (special action), with the
// special bytes 0xF0/0xF1 stamping a default wave/instrument, and block jumps
// (1-based jump byte -> 0-based block) terminated by an EOF marker (0xFF).
namespace rp::lsdj::codec {

inline constexpr std::size_t  kBlockSize  = 0x200; // 512
inline constexpr std::size_t  kBlockCount = 191;
inline constexpr std::uint8_t kEofBlock   = 0xFF;
inline constexpr std::uint8_t kEmptyBlock = 0xFF;

// Decompress one project's song from the block area (kBlockCount*kBlockSize
// bytes), starting at 0-based `startBlock`, following jumps. Returns the
// 0x8000-byte song, or an error if malformed / wrong size.
rfl::Result<std::vector<std::uint8_t>>
decompressProject(std::span<const std::uint8_t> blockArea, std::uint8_t startBlock);

struct Compressed {
    std::vector<std::uint8_t> bytes; // contiguous, block-padded (multiple of kBlockSize)
    std::size_t               blockCount = 0;
};

// Compress a 0x8000-byte song into a block stream whose jumps are numbered from
// 1-based `startBlock` (mirrors liblsdj compress_projects' currentBlock). Error
// if it would exceed kBlockCount.
rfl::Result<Compressed> compressProject(std::span<const std::uint8_t> song,
                                        std::uint8_t startBlock);

} // namespace rp::lsdj::codec
