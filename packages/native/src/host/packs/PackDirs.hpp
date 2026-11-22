#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Where packs live (docs/downloadable-packs-loading-design.md section 1): the installed packs and their state.json in
// the user's data directory, the bytecode compiled from their bundles in the user's cache directory, and the
// development pack folders RETROPLUG_PACK_PATH names. Every function that touches the disk takes a PackRoots, so a
// test hands it a temporary directory and never reaches the user's own folders.
namespace rp::host::packs {

struct PackRoots {
    std::filesystem::path data;   // <data>/state.json and <data>/<id>/<version>/
    std::filesystem::path cache;  // <cache>/<id>/<version>/, rebuilt whenever it is missing
};

// The user's roots on this platform: RETROPLUG_PACKS_DIR and RETROPLUG_PACKS_CACHE_DIR when set (the CLI's tests point
// them at a scratch folder, as RETROPLUG_USER_CONFIG_DIR does the config), else
//   Linux    $XDG_DATA_HOME/retroplug/packs (~/.local/share/...), $XDG_CACHE_HOME/retroplug/packs (~/.cache/...)
//   macOS    ~/Library/Application Support/RetroPlug/packs, ~/Library/Caches/RetroPlug/packs
//   Windows  %LOCALAPPDATA%\RetroPlug\packs, %LOCALAPPDATA%\RetroPlug\cache\packs
// A root it cannot find (no HOME, no LOCALAPPDATA) is empty, and a caller loads no installed pack.
PackRoots defaultPackRoots();

// The platform folder a pack's native libraries are under (native/<platform>/): linux-x64, linux-arm64,
// macos-universal or windows-x64. Empty on a platform no pack is built for, where no pack with a native half loads.
std::string_view currentPlatform();

// The file a pack's native half named `name` (pack.json's halves.native) is, inside native/<platform>/: rp-mesen.so,
// rp-mesen.dylib, rp-mesen.dll.
std::string libraryFileName(std::string_view name);

// The folders a RETROPLUG_PACK_PATH value names, in order: separated by ';' on Windows and ':' elsewhere, as PATH is,
// with empty entries dropped.
std::vector<std::filesystem::path> splitPackPath(std::string_view value);
#if defined(_WIN32)
// The same split of the wide form packPathFromEnv reads, so a profile path outside the ANSI code page survives.
std::vector<std::filesystem::path> splitPackPath(std::wstring_view value);
#endif

// splitPackPath(RETROPLUG_PACK_PATH), or nothing when it is unset.
std::vector<std::filesystem::path> packPathFromEnv();

// `dir` as the loader keys a folder: lexically normal and with no trailing separator, so "packs/a/1.0.0/" and
// "packs/a/1.0.0" are one folder to a placement check, a duplicate check and a refusal's name.
std::filesystem::path normalisedDir(const std::filesystem::path& dir);

// `p` with symlinks and ".." resolved as far as it exists, or `p` itself when that fails: a folder's identity.
std::filesystem::path canonicalOr(const std::filesystem::path& p);

// Whether `dir` lies strictly under `root`, both canonical (a symlink into the root counts, a sibling that only shares
// the name's prefix does not): a development folder that is really a folder the data root manages.
bool isUnder(const std::filesystem::path& dir, const std::filesystem::path& root);

// `p` as UTF-8 for a message or a comparison with an id. Never throws: on Windows path::string() converts through the
// ANSI code page and throws for a name outside it, which a refusal's message must not do.
std::string displayPath(const std::filesystem::path& p);

}  // namespace rp::host::packs
