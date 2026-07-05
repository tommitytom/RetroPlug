#include "BackendRpcService.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

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
