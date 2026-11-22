#include "host/packs/PackDirs.hpp"

#include <cstdlib>

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// An environment variable as a path, empty when it is unset or empty. Windows reads the wide form, so a profile path
// outside the ANSI code page survives.
fs::path envPath(const char* name) {
#if defined(_WIN32)
    const std::wstring wide(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wide.c_str());
    if (value == nullptr || *value == L'\0') return {};
    return fs::path(value);
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return {};
    return fs::path(value);
#endif
}

}  // namespace

PackRoots defaultPackRoots() {
    PackRoots roots;
    roots.data = envPath("RETROPLUG_PACKS_DIR");
    roots.cache = envPath("RETROPLUG_PACKS_CACHE_DIR");
#if defined(_WIN32)
    // The local profile, not the roaming %APPDATA%: a domain copies the roaming one between machines at each logon, and
    // a pack's native libraries are large and machine-specific.
    const fs::path local = envPath("LOCALAPPDATA");
    if (roots.data.empty() && !local.empty()) roots.data = local / "RetroPlug" / "packs";
    if (roots.cache.empty() && !local.empty()) roots.cache = local / "RetroPlug" / "cache" / "packs";
#elif defined(__APPLE__)
    const fs::path home = envPath("HOME");
    if (roots.data.empty() && !home.empty())
        roots.data = home / "Library" / "Application Support" / "RetroPlug" / "packs";
    if (roots.cache.empty() && !home.empty()) roots.cache = home / "Library" / "Caches" / "RetroPlug" / "packs";
#else
    const fs::path home = envPath("HOME");
    if (roots.data.empty()) {
        const fs::path xdg = envPath("XDG_DATA_HOME");
        if (!xdg.empty()) roots.data = xdg / "retroplug" / "packs";
        else if (!home.empty()) roots.data = home / ".local" / "share" / "retroplug" / "packs";
    }
    if (roots.cache.empty()) {
        const fs::path xdg = envPath("XDG_CACHE_HOME");
        if (!xdg.empty()) roots.cache = xdg / "retroplug" / "packs";
        else if (!home.empty()) roots.cache = home / ".cache" / "retroplug" / "packs";
    }
#endif
    return roots;
}

std::string_view currentPlatform() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "windows-x64";
#elif defined(__APPLE__)
    return "macos-universal";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return {};
#endif
}

std::string libraryFileName(std::string_view name) {
#if defined(_WIN32)
    return std::string(name) + ".dll";
#elif defined(__APPLE__)
    return std::string(name) + ".dylib";
#else
    return std::string(name) + ".so";
#endif
}

std::vector<fs::path> splitPackPath(std::string_view value) {
#if defined(_WIN32)
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif
    std::vector<fs::path> out;
    std::size_t at = 0;
    while (at <= value.size()) {
        std::size_t end = value.find(kSeparator, at);
        if (end == std::string_view::npos) end = value.size();
        if (end > at) out.emplace_back(std::string(value.substr(at, end - at)));
        at = end + 1;
    }
    return out;
}

std::string displayPath(const fs::path& p) {
#if defined(_WIN32)
    try {
        const std::u8string utf8 = p.u8string();
        return std::string(utf8.begin(), utf8.end());
    } catch (...) {
        return "<a path that is not valid UTF-16>";
    }
#else
    return p.string();
#endif
}

std::vector<fs::path> packPathFromEnv() {
    const fs::path value = envPath("RETROPLUG_PACK_PATH");
    if (value.empty()) return {};
#if defined(_WIN32)
    const std::wstring wide = value.native();
    std::vector<fs::path> out;
    std::size_t at = 0;
    while (at <= wide.size()) {
        std::size_t end = wide.find(L';', at);
        if (end == std::wstring::npos) end = wide.size();
        if (end > at) out.emplace_back(wide.substr(at, end - at));
        at = end + 1;
    }
    return out;
#else
    return splitPackPath(value.string());
#endif
}

}  // namespace rp::host::packs
