#pragma once

// What retroplug-packs-test's files share: a scratch folder standing in for the user's pack roots, the staged fixture
// packs (build/packs/<id>/1.0.0/, test/packs/fixture/CMakeLists.txt), and an environment variable set for one case.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "host/engine/Engine.hpp"
#include "host/packs/PackDirs.hpp"
#include "util/ProcessId.hpp"

namespace rp::host::packs::test {

// A fresh folder under the system's temporary directory, removed with everything in it when the case ends. Never the
// user's own data or cache directory: every case that touches a root touches one of these. Windows cannot delete a
// library a case loaded from one (a pack is never unloaded), so a folder may outlive its process there; the first
// TempDir of each run sweeps the ones whose process is gone.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> serial{0};
        static const bool swept = sweepAbandoned();
        (void)swept;
        const std::string name = std::string(kPrefix) + std::to_string(rp::currentProcessId()) + "-" +
                                 std::to_string(serial.fetch_add(1));
        path_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    PackRoots roots() const { return {path_ / "data", path_ / "cache"}; }

private:
    static constexpr const char* kPrefix = "retroplug-packs-test-";

    // Every retroplug-packs-test-<pid>-<n> folder whose process is no longer alive, removed.
    static bool sweepAbandoned() {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(kPrefix, 0) != 0) continue;
            const std::string rest = name.substr(std::string(kPrefix).size());
            long long pid = 0;
            try {
                pid = std::stoll(rest.substr(0, rest.find('-')));
            } catch (...) {
                continue;
            }
            if (pid == rp::currentProcessId() || rp::processAlive(pid)) continue;
            std::error_code gone;
            std::filesystem::remove_all(entry.path(), gone);
        }
        return true;
    }

    std::filesystem::path path_;
};

// A fixture pack's staged version folder.
inline std::filesystem::path fixture(const std::string& id) {
    return std::filesystem::path(RP_PACKS_BUILD_DIR) / id / "1.0.0";
}

// `id`'s staged folder copied into `root` as an installed pack (<root>/<id>/1.0.0/), its path answered.
inline std::filesystem::path install(const std::filesystem::path& root, const std::string& id,
                                     const std::string& version = "1.0.0") {
    const std::filesystem::path to = root / id / version;
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy(fixture(id), to, std::filesystem::copy_options::recursive);
    return to;
}

// An environment variable set (or unset, for nullopt) until the case ends, then put back as it was.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::optional<std::string> value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        set(value);
    }
    ~ScopedEnv() { set(old_); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void set(const std::optional<std::string>& value) {
#if defined(_WIN32)
        _putenv_s(name_, value ? value->c_str() : "");
#else
        if (value) setenv(name_, value->c_str(), 1);
        else unsetenv(name_);
#endif
    }
    const char*                name_;
    std::optional<std::string> old_;
};

// One host MIDI message an Engine sent out in a block: its system, frame and bytes.
struct MidiOut {
    std::uint32_t             system = 0, frame = 0;
    std::vector<std::uint8_t> data;
    bool operator==(const MidiOut&) const = default;
};

// One block over the staged messages, and the host MIDI the plan sent out.
inline std::vector<MidiOut> runBlock(Engine& e, const std::vector<std::vector<std::uint8_t>>& messages) {
    std::uint32_t frame = 0;
    for (const auto& m : messages) e.stageMidi(frame++, m);
    std::vector<float> l(256), r(256);
    e.processBlock(256, l.data(), r.data());
    std::vector<MidiOut> out;
    for (const auto& m : e.midiOut()) out.push_back({m.system, m.frame, m.data});
    return out;
}

}  // namespace rp::host::packs::test
