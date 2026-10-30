#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <vector>

/** Whole file into a byte vector: no value when the file cannot be opened or cannot be READ, an EMPTY vector when it
 *  opens and holds nothing (a zero-byte battery is a blank one, not a failure).
 *
 *  Opening and reading are two questions and the stream's good state answers only the first: a path naming a directory
 *  opens, and it is the read that fails. So the read goes by the size the stream reports rather than running to the
 *  end, because a read that stops short of that size is a failure rather than an end. That difference is what this is
 *  for: a copy that stopped early is a short or empty vector, which putResource would hand the core as a blank battery
 *  for it to auto-save over the real save. The size is only ever what a whole read must add up to, never an allocation:
 *  what a stream reports for a directory is the largest offset the platform can name rather than an error, so the file
 *  arrives in chunks and a failing one answers before anything large has been reserved.
 *
 *  EngineRpcService's putResource is its only caller - a resource is read native so a battery or a 32 MB ROM never
 *  crosses the RPC. Note what it deliberately is NOT: the tree has three other file readers - N8Host::readFileBytes
 *  THROWS, HostRpcService::slurp returns an optional and truncates at a cap, and SampleCache::readFile, whose answer
 *  about an empty file against a failed one this now matches and whose spelling (a bool and an out-param) it does not
 *  take. Those stay where they are; folding them in here would mean picking one error contract for callers that each
 *  need a different one. */
inline std::optional<std::vector<std::uint8_t>> slurpAll(const std::string& path) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        in.seekg(0, std::ios::end);
        const std::streamsize size = in.tellg();
        if (size < 0) return std::nullopt;  // a stream that reports no size at all: a pipe, a character device
        in.seekg(0, std::ios::beg);
        constexpr std::size_t kChunk = 64 * 1024;
        const std::size_t     total  = static_cast<std::size_t>(size);
        std::vector<std::uint8_t> bytes;
        while (bytes.size() < total) {
            const std::size_t at   = bytes.size();
            const std::size_t want = std::min(total - at, kChunk);
            // The vector's own growth factor absorbs these, so a 32 MB ROM is copied a few times rather than often.
            bytes.insert(bytes.end(), want, std::uint8_t{});
            in.read(reinterpret_cast<char*>(bytes.data() + at), static_cast<std::streamsize>(want));
            // A read that stops short is the failure this exists to catch: an error of the stream's own (a directory,
            // an EIO), or a file truncated underneath the size it reported.
            if (static_cast<std::size_t>(in.gcount()) != want) return std::nullopt;
        }
        return bytes;
    } catch (const std::exception&) {
        // The caller answers "cannot read" and creates nothing; an exception here would leave the RPC with no reply.
        return std::nullopt;
    }
}
