#pragma once

#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

/** Whole file into a byte vector: no value when the file cannot be opened or cannot be read (a path naming a directory
 *  opens, and the failing read arrives as the stream's own exception: that is the same answer), an EMPTY vector when
 *  it opens and holds nothing (a zero-byte battery is a blank one, not a failure).
 *
 *  EngineRpcService's putResource is its only caller - a resource is read native so a battery or a 32 MB ROM never
 *  crosses the RPC. Note what it deliberately is NOT: the tree has three other file readers with genuinely different
 *  contracts - N8Host::readFileBytes THROWS, HostRpcService::slurp returns an optional and truncates at a cap, and
 *  SampleCache::readFile distinguishes an empty file from a failure. Those stay where they are; folding them in here
 *  would mean picking one error contract for callers that each need a different one. */
inline std::optional<std::vector<std::uint8_t>> slurpAll(const std::string& path) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } catch (const std::exception&) {
        // The caller answers "cannot read" and creates nothing; an exception here would leave the RPC with no reply.
        return std::nullopt;
    }
}
