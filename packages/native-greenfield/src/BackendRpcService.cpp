#include "BackendRpcService.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

#include "StubSystem.hpp"
#include "util/MinizZip.hpp"

namespace fs = std::filesystem;

namespace {

// The per-OS config dir. Reimplemented here (rather than linking native's
// UserConfigPaths.cpp) so this host stays free of UserConfig.hpp / efsw; the logic
// mirrors packages/native/src/config/UserConfigPaths.cpp exactly.
fs::path resolveConfigDir() {
    if (const char* env = std::getenv("RETROPLUG_USER_CONFIG_DIR"); env && *env)
        return fs::path(env);
#if defined(_WIN32)
    if (const char* env = std::getenv("APPDATA"); env && *env) return fs::path(env) / "RetroPlug";
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / "Library" / "Application Support" / "RetroPlug";
    return {};
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "retroplug";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".config" / "retroplug";
    return {};
#endif
}

rfl::Bytestring toBytestring(const std::vector<std::uint8_t>& v) {
    const auto* p = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(p, p + v.size());
}

// Read a file's bytes (or the first `maxBytes`), or nullopt when it can't be opened.
std::optional<std::vector<std::uint8_t>> slurp(const std::string& path, std::size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> buf(maxBytes);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(maxBytes));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

std::size_t fileSizeOr(const std::string& path, std::size_t fallback) {
    std::error_code ec;
    const auto n = fs::file_size(path, ec);
    return ec ? fallback : static_cast<std::size_t>(n);
}

// Deterministic, id-tagged stand-ins for a live system's pump bytes (mirrors the mock's
// "SR"/"ST" + little-endian id): legible in a hexdump and stable across a round-trip, so an
// export → import → read comparison is byte-exact.
std::vector<std::uint8_t> defaultSram(SystemId id) {
    return { 'S', 'R', static_cast<std::uint8_t>(id & 0xff), static_cast<std::uint8_t>((id >> 8) & 0xff) };
}
std::vector<std::uint8_t> defaultState(SystemId id) {
    return { 'S', 'T', static_cast<std::uint8_t>(id & 0xff), static_cast<std::uint8_t>((id >> 8) & 0xff) };
}

} // namespace

std::optional<rfl::Bytestring> BackendRpcService::readFile(std::string path) {
    auto bytes = slurp(path, fileSizeOr(path, 0));
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

std::optional<rfl::Bytestring> BackendRpcService::readFilePrefix(std::string path, std::uint32_t length) {
    auto bytes = slurp(path, length);
    if (!bytes) return std::nullopt;
    return toBytestring(*bytes);
}

bool BackendRpcService::writeFile(std::string path, std::vector<std::uint8_t> bytes) {
    // Create parent dirs on demand — the mock backend is dir-free, and the stores (e.g.
    // BindingsStore writing bindings/<name>.json) rely on that forgiving behaviour.
    std::error_code ec;
    if (const fs::path parent = fs::path(path).parent_path(); !parent.empty())
        fs::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty())
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

bool BackendRpcService::writeFileAtomic(std::string path, std::vector<std::uint8_t> bytes) {
    const std::string tmp = path + ".tmp";
    if (!writeFile(tmp, std::move(bytes))) return false;
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool BackendRpcService::fileExists(std::string path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool BackendRpcService::rename(std::string from, std::string to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
}

std::vector<std::string> BackendRpcService::listDir(std::string dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        out.push_back(entry.path().filename().string());
    }
    return out;
}

bool BackendRpcService::deleteFile(std::string path) {
    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    return removed && !ec;
}

std::vector<std::string> BackendRpcService::drainChangedPaths() {
    return {};
}

std::string BackendRpcService::canonicalize(std::string path) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(path, ec);
    return ec ? path : c.string();
}

std::string BackendRpcService::configDir() {
    return resolveConfigDir().string();
}

rfl::Bytestring BackendRpcService::zip(std::vector<BackendZipInput> entries) {
    MinizWriter w;
    for (const auto& e : entries)
        if (!w.add(e.name, e.bytes)) return {};
    return toBytestring(w.finish());
}

std::vector<BackendZipEntry> BackendRpcService::unzip(std::vector<std::uint8_t> bytes) {
    std::vector<BackendZipEntry> out;
    MinizReader r(bytes);
    if (!r.valid()) return out;
    for (const auto& name : r.names())
        out.push_back({ name, toBytestring(r.read(name)) });
    return out;
}

// --- emulator lifecycle / reads --------------------------------------------
// Every system in this host is a StubSystem; static_cast is safe (and RTTI-free).

std::optional<std::uint32_t> BackendRpcService::constructSystem(BackendConstructSpec spec) {
    if (spec.embeddedRom.empty()) {
        // File-backed: native slurps the ROM. An unreadable file is the only native reject —
        // the TS store already classified the format before calling.
        if (!slurp(spec.romPath, fileSizeOr(spec.romPath, 0))) return std::nullopt;
    }

    const SystemId id = project_.nextSystemId();
    auto sys = std::make_unique<StubSystem>(id, spec.romPath, spec.embeddedRom, spec.savPath.value_or(""));

    // Seed SRAM: zip-import bytes win; else the on-disk .sav if present; else a default.
    if (spec.sramBytes) {
        sys->loadSramBytes(*spec.sramBytes);
    } else if (spec.savPath) {
        if (auto s = slurp(*spec.savPath, fileSizeOr(*spec.savPath, 0))) sys->loadSramBytes(*s);
        else sys->loadSramBytes(defaultSram(id));
    } else {
        sys->loadSramBytes(defaultSram(id));
    }
    // Seed savestate: zip-import bytes, else a default (no cold-boot core to snapshot).
    sys->loadStateBytes(spec.stateBytes ? *spec.stateBytes : defaultState(id));

    sys->onActivate(sampleRate_);
    if (spec.replaceId) project_.removeSystem(*spec.replaceId);  // swap in place
    project_.adoptSystem(sys.release());
    project_.rebuildLinkGroups();
    return id;
}

std::optional<std::uint32_t> BackendRpcService::duplicateSystem(std::uint32_t srcId,
                                                                std::optional<std::string> savPath) {
    SystemBase* src = project_.findSystem(srcId);
    if (!src) return std::nullopt;
    const auto* stub = static_cast<StubSystem*>(src);

    const SystemId id = project_.nextSystemId();
    auto sys = std::make_unique<StubSystem>(id, src->romPath(), stub->embeddedRom(), savPath.value_or(""));
    sys->loadSramBytes(src->saveSramBytes());   // clone the live state
    sys->loadStateBytes(src->saveStateBytes());
    sys->onActivate(sampleRate_);
    project_.adoptSystem(sys.release());
    project_.rebuildLinkGroups();
    return id;
}

std::optional<std::uint32_t> BackendRpcService::reloadSystem(std::uint32_t id) {
    SystemBase* old = project_.findSystem(id);
    if (!old) return std::nullopt;
    const auto* stub = static_cast<StubSystem*>(old);

    const std::string rom = old->romPath();
    const std::string embedded = stub->embeddedRom();
    const std::string sav = old->savPath();
    const auto sram = old->saveSramBytes();     // carry live SRAM forward

    const SystemId newId = project_.nextSystemId();
    auto sys = std::make_unique<StubSystem>(newId, rom, embedded, sav);
    sys->loadSramBytes(sram);
    sys->loadStateBytes(defaultState(newId));   // reload drops the savestate
    sys->onActivate(sampleRate_);
    project_.removeSystem(id);                   // swap in place, fresh id
    project_.adoptSystem(sys.release());
    project_.rebuildLinkGroups();
    return newId;
}

bool BackendRpcService::removeSystem(std::uint32_t id) {
    if (!project_.findSystem(id)) return false;
    project_.removeSystem(id);
    project_.rebuildLinkGroups();
    return true;
}

bool BackendRpcService::applySystemSetting(std::uint32_t id, std::string /*key*/, double /*value*/) {
    return project_.findSystem(id) != nullptr;
}

bool BackendRpcService::applyRoleConfig(std::uint32_t id, std::string /*kind*/, std::string /*config*/) {
    return project_.findSystem(id) != nullptr;
}

std::optional<rfl::Bytestring> BackendRpcService::readState(std::uint32_t id) {
    SystemBase* s = project_.findSystem(id);
    if (!s) return std::nullopt;
    return toBytestring(s->saveStateBytes());
}

std::optional<rfl::Bytestring> BackendRpcService::readSram(std::uint32_t id) {
    SystemBase* s = project_.findSystem(id);
    if (!s) return std::nullopt;
    return toBytestring(s->saveSramBytes());
}
