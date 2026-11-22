#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A pack's manifest, pack.json (docs/downloadable-packs-loading-design.md section 1), parsed and checked: every member
// is held to its shape here, so a manifest a later milestone reads is one this parser already accepted whole. What M1's
// loader (PackTable) enforces beyond the shape is the native half and provides.nodeTypes; it refuses a pack naming any
// other half, since M1 cannot load a TS half and a pack never loads in part. provides.platforms and .roles, and
// requires.platforms, are M2's to enforce.
namespace rp::host::packs {

struct PackManifest {
    std::string   id;        // a plain id: lowercase letters, digits and '-', starting with a letter
    std::string   name;      // what Settings > Packs shows
    std::string   version;   // MAJOR.MINOR.PATCH, the name of its folder under packs/<id>/
    std::uint32_t apiLevel = 0;
    std::string   license;   // an SPDX expression, shown beside the pack
    std::string   source;    // where its source lives
    std::vector<std::string> requiresPacks;      // pack ids loaded before it
    std::vector<std::string> requiresPlatforms;  // platform ids some loaded pack provides (checked by M2)
    std::vector<std::string> providesPlatforms;
    std::vector<std::string> providesNodeTypes;  // every node type its native half may register
    std::vector<std::string> providesRoles;
    // Its halves by kind: "native" names the library stem, which is always "rp-<id>" (native/<platform>/rp-<id>.so:
    // bound to the id, so two packs' libraries never share a file name, which Windows matches a loaded module by), the
    // others a JS file inside the version folder. A kind no RetroPlug knows is refused here.
    std::map<std::string, std::string> halves;
    // Set only by a parse given a host level (below) that this manifest's apiLevel differs from: only id and apiLevel
    // are read then, since a manifest of another level may spell every other member in ways this host does not know.
    bool levelMismatch = false;

    // The native half's library stem, or empty for a pack with no native half.
    std::string nativeHalf() const;
};

// The kinds of half this host knows, in the order the loader loads them.
const std::vector<std::string>& knownHalves();

// Whether `id` is a plain pack id: 1 to 64 characters, [a-z][a-z0-9-]*.
bool isPackId(std::string_view id);

// Whether `version` is MAJOR.MINOR.PATCH in decimal with no leading zero: it names a folder, so nothing else may.
bool isPackVersion(std::string_view version);

// `json` as a manifest, or nothing with the reason in *why (each reason starts "pack.json: "). With a non-zero
// `hostLevel`, a manifest whose apiLevel differs is answered as soon as its size, nesting and duplicate keys have been
// checked (those gates always run first, for safety), with levelMismatch set and only id and apiLevel filled: a pack
// of another level is refused for its level, whatever else it has changed since this host's.
std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why, std::uint32_t hostLevel = 0);

// <versionDir>/pack.json read and parsed; nothing, with the reason, when it cannot be read or does not parse.
std::optional<PackManifest> readPackManifest(const std::filesystem::path& versionDir, std::string* why,
                                             std::uint32_t hostLevel = 0);

}  // namespace rp::host::packs
