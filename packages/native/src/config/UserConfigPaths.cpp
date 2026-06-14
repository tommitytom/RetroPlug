#include "UserConfig.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
    #include <windows.h>
    #include <shlobj.h>
    #include <combaseapi.h>
#endif

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
fs::path roamingAppData() {
    PWSTR widePath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData,
                                      KF_FLAG_CREATE,
                                      nullptr,
                                      &widePath);
    if (FAILED(hr) || !widePath) {
        if (widePath) CoTaskMemFree(widePath);
        // Last-ditch fallback — APPDATA env var.
        if (const char* env = std::getenv("APPDATA"); env && *env)
            return fs::path(env);
        return {};
    }
    fs::path out(widePath);
    CoTaskMemFree(widePath);
    return out;
}
#endif

} // namespace

fs::path resolveDefaultUserConfigDir() {
    // Test/debug override — same convention as RETROPLUG_AUTOLOAD_*.
    if (const char* env = std::getenv("RETROPLUG_USER_CONFIG_DIR");
        env && *env) {
        return fs::path(env);
    }

#if defined(_WIN32)
    // %APPDATA%\RetroPlug — modern best practice (FOLDERID_RoamingAppData).
    fs::path root = roamingAppData();
    if (!root.empty()) return root / "RetroPlug";
    return {};
#elif defined(__APPLE__)
    // ~/Library/Application Support/RetroPlug per Apple HIG.
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / "Library" / "Application Support" / "RetroPlug";
    return {};
#else
    // XDG Base Dir spec: $XDG_CONFIG_HOME/retroplug, fallback ~/.config/retroplug.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return fs::path(xdg) / "retroplug";
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".config" / "retroplug";
    return {};
#endif
}
