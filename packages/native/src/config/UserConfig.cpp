#include "UserConfig.hpp"

#include "config/SchemaVersions.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamsize size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!in.read(out.data(), size)) return {};
    return out;
}

bool endsWithJson(const std::string& name) {
    return name.size() >= 5 &&
           name.compare(name.size() - 5, 5, ".json") == 0;
}

} // namespace

UserConfig::UserConfig(fs::path rootOverride)
    : rootDirOverride_(std::move(rootOverride)) {
    // Seed the in-memory snapshot with the same defaults that will be
    // written to disk on first run. This way the UI has working bindings
    // even before start() has finished parsing — and if start() fails
    // outright (read-only home dir, missing permissions) the UI still
    // works with hardcoded defaults.
    current_.activeKeyboardBindings = "default";
    current_.activeGamepadBindings  = "default";
    current_.bindings               = defaultBindingMap();
}

UserConfig::~UserConfig() {
    stop();
}

void UserConfig::start() {
    if (started_.exchange(true)) return;

    rootDir_ = rootDirOverride_.empty()
        ? resolveDefaultUserConfigDir()
        : rootDirOverride_;
    bindingsDir_ = rootDir_ / "bindings";
    configFile_  = rootDir_ / "config.json";

    std::error_code ec;
    fs::create_directories(bindingsDir_, ec);
    if (ec) {
        std::fprintf(stderr, "[user-config] failed to create %s: %s\n",
                     bindingsDir_.string().c_str(), ec.message().c_str());
        return;
    }

    writeDefaultsIfMissing();
    reloadFromDisk();

    watcher_ = std::make_unique<efsw::FileWatcher>();
    rootWatchId_     = watcher_->addWatch(rootDir_.string(),     this, false);
    bindingsWatchId_ = watcher_->addWatch(bindingsDir_.string(), this, false);
    watcher_->watch();
}

void UserConfig::stop() {
    if (!started_.exchange(false)) return;
    if (watcher_) {
        watcher_->removeWatch(rootWatchId_);
        watcher_->removeWatch(bindingsWatchId_);
        watcher_.reset();
    }
}

void UserConfig::handleFileAction(efsw::WatchID,
                                  const std::string&,
                                  const std::string& filename,
                                  efsw::Action,
                                  const std::string&) {
    // Editors often save via tmp + rename. The rename produces an Add or
    // Moved event on the final name. Ignore everything that isn't a
    // *.json — keeps editor swap files (.swp, .swx, foo.json~) from
    // triggering a parse we'd just discard.
    if (!endsWithJson(filename)) return;
    dirty_.store(true, std::memory_order_release);
}

void UserConfig::pumpReloadsOnUiThread() {
    if (!dirty_.exchange(false, std::memory_order_acquire)) return;
    reloadFromDisk();
    if (onReload_) onReload_();
}

UserConfigDto UserConfig::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_;
}

bool UserConfig::setActiveKeyboardBindings(std::string name) {
    if (!started_.load()) return false;
    UserConfigJson cfg;
    cfg.schemaVersion = rp::schema::kUserConfig;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg.activeKeyboardBindings = std::move(name);
        cfg.activeGamepadBindings  = current_.activeGamepadBindings;
        cfg.defaultZoom            = current_.defaultZoom;
        cfg.sramMirror             = current_.sramMirror;
    }
    if (!atomicWrite(configFile_, userConfigToJson(cfg))) {
        std::fprintf(stderr, "[user-config] failed to write %s\n",
                     configFile_.string().c_str());
        return false;
    }
    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

bool UserConfig::setActiveGamepadBindings(std::string name) {
    if (!started_.load()) return false;
    UserConfigJson cfg;
    cfg.schemaVersion = rp::schema::kUserConfig;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg.activeKeyboardBindings = current_.activeKeyboardBindings;
        cfg.activeGamepadBindings  = std::move(name);
        cfg.defaultZoom            = current_.defaultZoom;
        cfg.sramMirror             = current_.sramMirror;
    }
    if (!atomicWrite(configFile_, userConfigToJson(cfg))) {
        std::fprintf(stderr, "[user-config] failed to write %s\n",
                     configFile_.string().c_str());
        return false;
    }
    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

bool UserConfig::setSramMirror(rp::SramMirror mode) {
    if (!started_.load()) return false;
    UserConfigJson cfg;
    cfg.schemaVersion = rp::schema::kUserConfig;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (current_.sramMirror == mode) return true; // no-op write
        cfg.activeKeyboardBindings = current_.activeKeyboardBindings;
        cfg.activeGamepadBindings  = current_.activeGamepadBindings;
        cfg.defaultZoom            = current_.defaultZoom;
        cfg.sramMirror             = mode;
    }
    if (!atomicWrite(configFile_, userConfigToJson(cfg))) {
        std::fprintf(stderr, "[user-config] failed to write %s\n",
                     configFile_.string().c_str());
        return false;
    }
    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

rp::SramMirror UserConfig::sramMirror() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_.sramMirror;
}

bool UserConfig::setDefaultZoom(std::uint8_t zoom) {
    if (!started_.load()) return false;
    if (zoom < 1 || zoom > 6) return false;
    UserConfigJson cfg;
    cfg.schemaVersion = rp::schema::kUserConfig;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (current_.defaultZoom == zoom) return true; // no-op write
        cfg.activeKeyboardBindings = current_.activeKeyboardBindings;
        cfg.activeGamepadBindings  = current_.activeGamepadBindings;
        cfg.defaultZoom            = zoom;
        cfg.sramMirror             = current_.sramMirror;
    }
    if (!atomicWrite(configFile_, userConfigToJson(cfg))) {
        std::fprintf(stderr, "[user-config] failed to write %s\n",
                     configFile_.string().c_str());
        return false;
    }
    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

std::optional<BindingMapJson> UserConfig::loadProfile(std::string_view name) const {
    if (!isValidProfileName(name)) return std::nullopt;
    const fs::path p = bindingsDir_ / (std::string(name) + ".json");
    auto text = slurp(p);
    if (text.empty()) return std::nullopt;
    return bindingMapFromJson(text);
}

bool UserConfig::isValidProfileName(std::string_view name) {
    if (name.empty()) return false;
    if (name == "config") return false;       // would collide with config.json
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z')
                     || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9')
                     ||  c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool UserConfig::saveProfile(std::string name, BindingMapJson bindings) {
    if (!started_.load()) return false;
    if (!isValidProfileName(name)) return false;
    bindings.schemaVersion = rp::schema::kBindings;
    bindings.name          = name;
    const fs::path target  = bindingsDir_ / (name + ".json");
    if (!atomicWrite(target, bindingMapToJson(bindings))) {
        std::fprintf(stderr, "[user-config] failed to write %s\n",
                     target.string().c_str());
        return false;
    }
    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

bool UserConfig::renameProfile(std::string oldName, std::string newName) {
    if (!started_.load()) return false;
    if (!isValidProfileName(oldName) || !isValidProfileName(newName)) return false;
    if (oldName == newName) return true;
    const fs::path src = bindingsDir_ / (oldName + ".json");
    const fs::path dst = bindingsDir_ / (newName + ".json");
    std::error_code ec;
    if (!fs::exists(src, ec)) return false;
    if (fs::exists(dst, ec)) return false;     // refuse to clobber

    // If the source carries an embedded `name` field, rewrite it so the
    // file's content matches its new filename (cosmetic — the loader uses
    // the filename, not the field). Keeps hand-inspection sane.
    if (auto text = slurp(src); !text.empty()) {
        if (auto parsed = bindingMapFromJson(text)) {
            parsed->name = newName;
            const fs::path tmpRename = src.string() + ".renaming";
            if (!atomicWrite(tmpRename, bindingMapToJson(*parsed))) return false;
            fs::rename(tmpRename, src, ec);
            if (ec) { fs::remove(tmpRename, ec); return false; }
        }
    }

    fs::rename(src, dst, ec);
    if (ec) return false;

    // Repoint active profile references in config.json if needed.
    bool rewrote = false;
    UserConfigJson cfg;
    cfg.schemaVersion = rp::schema::kUserConfig;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg.activeKeyboardBindings = current_.activeKeyboardBindings;
        cfg.activeGamepadBindings  = current_.activeGamepadBindings;
        cfg.defaultZoom            = current_.defaultZoom;
        cfg.sramMirror             = current_.sramMirror;
        if (cfg.activeKeyboardBindings == oldName) {
            cfg.activeKeyboardBindings = newName;
            rewrote = true;
        }
        if (cfg.activeGamepadBindings == oldName) {
            cfg.activeGamepadBindings = newName;
            rewrote = true;
        }
    }
    if (rewrote) {
        if (!atomicWrite(configFile_, userConfigToJson(cfg))) {
            std::fprintf(stderr, "[user-config] failed to update %s after rename\n",
                         configFile_.string().c_str());
            return false;
        }
    }

    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

bool UserConfig::deleteProfile(std::string name) {
    if (!started_.load()) return false;
    if (!isValidProfileName(name)) return false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (name == current_.activeKeyboardBindings) return false;
        if (name == current_.activeGamepadBindings)  return false;
    }
    const fs::path target = bindingsDir_ / (name + ".json");
    std::error_code ec;
    if (!fs::exists(target, ec)) return false;
    fs::remove(target, ec);
    if (ec) return false;
    reloadFromDisk();
    if (onReload_) onReload_();
    return true;
}

void UserConfig::reloadFromDisk() {
    UserConfigDto next;
    {
        std::lock_guard<std::mutex> lock(mu_);
        next = current_;
    }

    // config.json: keep previous active profiles on parse failure, and on a
    // version stamped newer than this build understands (don't apply a future
    // format we might misread — mirrors the parse-fail degradation).
    if (auto cfgText = slurp(configFile_); !cfgText.empty()) {
        if (auto cfg = userConfigFromJson(cfgText);
            cfg && rp::schema::checkVersion(cfg->schemaVersion, rp::schema::kUserConfig)
                       == rp::schema::Check::Newer) {
            std::fprintf(stderr,
                "[user-config] %s is schemaVersion %d (this build understands %d) "
                "— keeping previous settings\n",
                configFile_.string().c_str(), cfg->schemaVersion, rp::schema::kUserConfig);
        } else if (cfg) {
            next.activeKeyboardBindings = cfg->activeKeyboardBindings;
            next.activeGamepadBindings  = cfg->activeGamepadBindings;
            // Clamp to the supported zoom range; out-of-range values
            // fall back to 3 rather than producing a broken layout.
            std::uint8_t z = cfg->defaultZoom;
            if (z < 1 || z > 6) z = 3;
            next.defaultZoom = z;
            next.sramMirror = cfg->sramMirror;
        } else {
            std::fprintf(stderr,
                "[user-config] %s parse failed — keeping previous active profiles\n",
                configFile_.string().c_str());
        }
    }

    // bindings/<keyboardProfile>.json supplies .keyboard;
    // bindings/<gamepadProfile>.json supplies .gamepad. The two may be the
    // same file (the common case). On parse failure, retain the previous
    // value of that channel.
    auto loadBlock = [this](const std::string& profile)
        -> std::optional<BindingMapJson> {
        const fs::path p = bindingsDir_ / (profile + ".json");
        auto text = slurp(p);
        if (text.empty()) return std::nullopt;
        auto parsed = bindingMapFromJson(text);
        if (!parsed) {
            std::fprintf(stderr,
                "[user-config] %s parse failed — keeping previous bindings\n",
                p.string().c_str());
            return std::nullopt;
        }
        if (rp::schema::checkVersion(parsed->schemaVersion, rp::schema::kBindings)
                == rp::schema::Check::Newer) {
            std::fprintf(stderr,
                "[user-config] %s is schemaVersion %d (this build understands %d) "
                "— keeping previous bindings\n",
                p.string().c_str(), parsed->schemaVersion, rp::schema::kBindings);
            return std::nullopt;
        }
        return parsed;
    };
    if (auto kb = loadBlock(next.activeKeyboardBindings)) {
        next.bindings.keyboard = std::move(kb->keyboard);
    }
    if (auto gp = loadBlock(next.activeGamepadBindings)) {
        next.bindings.gamepad = std::move(gp->gamepad);
    }
    next.bindings.name = next.activeKeyboardBindings;

    // Profile list: enumerate every *.json under bindings/.
    next.availableProfiles.clear();
    std::error_code ec;
    if (fs::exists(bindingsDir_, ec)) {
        for (const auto& entry : fs::directory_iterator(bindingsDir_, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".json")
                next.availableProfiles.push_back(entry.path().stem().string());
        }
    }
    std::sort(next.availableProfiles.begin(), next.availableProfiles.end());

    std::lock_guard<std::mutex> lock(mu_);
    current_ = std::move(next);
}

void UserConfig::writeDefaultsIfMissing() {
    if (!fs::exists(configFile_)) {
        UserConfigJson cfg;
        cfg.activeKeyboardBindings = "default";
        cfg.activeGamepadBindings  = "default";
        atomicWrite(configFile_, userConfigToJson(cfg));
    }

    const fs::path defaultBinding = bindingsDir_ / "default.json";
    if (!fs::exists(defaultBinding)) {
        atomicWrite(defaultBinding, bindingMapToJson(defaultBindingMap()));
    }
}

bool UserConfig::atomicWrite(const fs::path& target, const std::string& contents) {
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}
