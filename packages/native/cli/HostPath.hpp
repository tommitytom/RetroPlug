#pragma once

// Shared by the CLI test harness (TestHarnessImpl/HarnessRpcService) and the
// headless UI test harness (UiTestHarness). Tests + tools use POSIX "/tmp/..."
// literals for scratch output (WAVs, PNGs, .rplg fixtures, project files). On
// Windows a bare "/tmp/x" lands in C:\tmp — a messy, non-standard drive-root
// dir — so redirect it into a tidy subdir of the OS temp dir (%TEMP%\retroplug\),
// the proper Windows temp via std::filesystem. Applied at every harness path
// boundary (writes, reads, AND the paths fed into recent-files / project load)
// so files and the paths they're tracked under stay consistent — no per-test
// edits. No-op on POSIX (where /tmp is correct) and for any non-/tmp path.

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace rpcli {

inline std::string resolveHostPath(const std::string& path) {
#ifdef _WIN32
    constexpr std::string_view kTmp = "/tmp/";
    if (path.size() >= kTmp.size() &&
        std::string_view(path).substr(0, kTmp.size()) == kTmp) {
        std::error_code ec;
        std::filesystem::path target =
            std::filesystem::temp_directory_path(ec) / "retroplug" / path.substr(kTmp.size());
        std::filesystem::create_directories(target.parent_path(), ec);
        return target.string();
    }
#endif
    return path;
}

} // namespace rpcli
