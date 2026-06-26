// Tests for the per-user config + bindings watcher.
//
// The watcher's reload path is normally driven by efsw on a bg thread; for
// determinism most assertions go through setActiveBindings() (synchronous
// reload). One test exercises the efsw path with a bounded retry loop.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

#include "config/UserConfig.hpp"
#include "config/UserConfigSerialization.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

fs::path makeTempDir(const std::string& tag) {
    auto base = fs::temp_directory_path();
    std::random_device rd;
    auto dir = base / ("retroplug-userconfig-" + tag + "-" +
                       std::to_string(rd()));
    fs::create_directories(dir);
    return dir;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    auto size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0);
    std::string out(static_cast<size_t>(size), '\0');
    in.read(out.data(), size);
    return out;
}

void writeFile(const fs::path& p, const std::string& contents) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// efsw is async — retry a small number of times to let it deliver events.
template<typename Pred>
bool waitFor(UserConfig& cfg, Pred pred, std::chrono::milliseconds budget = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        cfg.pumpReloadsOnUiThread();
        if (pred()) return true;
        std::this_thread::sleep_for(50ms);
    }
    cfg.pumpReloadsOnUiThread();
    return pred();
}

} // namespace

TEST_CASE("UserConfig writes defaults on first run", "[user-config]") {
    auto dir = makeTempDir("first-run");

    UserConfig cfg(dir);
    cfg.start();

    REQUIRE(fs::exists(dir / "config.json"));
    REQUIRE(fs::exists(dir / "bindings" / "default.json"));

    auto snap = cfg.snapshot();
    REQUIRE(snap.activeKeyboardBindings == "default");
    REQUIRE(snap.activeGamepadBindings  == "default");
    REQUIRE_FALSE(snap.bindings.keyboard.empty());
    REQUIRE(snap.bindings.keyboard.at("A") == std::vector<std::string>{"Z", "z"});
    REQUIRE(snap.bindings.gamepad.at("Start") == std::vector<std::string>{"start"});
    REQUIRE_FALSE(snap.availableProfiles.empty());

    fs::remove_all(dir);
}

TEST_CASE("UserConfig preserves existing files on second run", "[user-config]") {
    auto dir = makeTempDir("second-run");

    // First boot writes defaults.
    {
        UserConfig cfg(dir);
        cfg.start();
    }

    // User edits bindings/default.json by hand.
    BindingMapJson custom;
    custom.name = "default";
    custom.keyboard["A"]      = {"q"};
    custom.keyboard["B"]      = {"w"};
    custom.keyboard["Start"]  = {"Enter"};
    custom.keyboard["Select"] = {"ShiftL"};
    custom.gamepad["A"]       = {"a"};
    writeFile(dir / "bindings" / "default.json", bindingMapToJson(custom));

    // Second boot reads the edit, doesn't clobber.
    UserConfig cfg(dir);
    cfg.start();

    auto snap = cfg.snapshot();
    REQUIRE(snap.bindings.keyboard.at("A") == std::vector<std::string>{"q"});
    REQUIRE(snap.bindings.keyboard.at("B") == std::vector<std::string>{"w"});

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::setActiveKeyboardBindings switches profile synchronously", "[user-config]") {
    auto dir = makeTempDir("set-active");

    UserConfig cfg(dir);
    cfg.start();

    // Create an alt profile alongside the auto-written default.
    BindingMapJson alt;
    alt.name = "alt";
    alt.keyboard["A"]     = {"j"};
    alt.keyboard["B"]     = {"k"};
    alt.keyboard["Start"] = {"Enter"};
    writeFile(dir / "bindings" / "alt.json", bindingMapToJson(alt));

    REQUIRE(cfg.setActiveKeyboardBindings("alt"));

    auto snap = cfg.snapshot();
    REQUIRE(snap.activeKeyboardBindings == "alt");
    REQUIRE(snap.bindings.keyboard.at("A") == std::vector<std::string>{"j"});

    // config.json on disk now records the switch.
    auto cfgText = slurp(dir / "config.json");
    REQUIRE(cfgText.find("\"alt\"") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::setAutoSaveSram persists and sticks across reloads", "[user-config]") {
    auto dir = makeTempDir("autosave");

    {
        UserConfig cfg(dir);
        cfg.start();
        REQUIRE_FALSE(cfg.autoSaveSram());          // default off
        REQUIRE(cfg.setAutoSaveSram(true));
        REQUIRE(cfg.autoSaveSram());
        REQUIRE(cfg.snapshot().autoSaveSram);

        // config.json on disk records the flag.
        auto cfgText = slurp(dir / "config.json");
        REQUIRE(cfgText.find("autoSaveSram") != std::string::npos);
    }

    // A fresh instance over the same dir reads the preference back (sticky).
    {
        UserConfig cfg(dir);
        cfg.start();
        REQUIRE(cfg.autoSaveSram());

        // Switching a binding profile must NOT clobber the flag.
        REQUIRE(cfg.setActiveKeyboardBindings("default"));
        REQUIRE(cfg.autoSaveSram());
    }

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::setDefaultZoom persists, clamps, and sticks across reloads", "[user-config]") {
    auto dir = makeTempDir("defaultzoom");

    {
        UserConfig cfg(dir);
        cfg.start();
        REQUIRE(cfg.snapshot().defaultZoom == 3);    // baked-in default

        // Out-of-range values are rejected and leave the value untouched.
        REQUIRE_FALSE(cfg.setDefaultZoom(0));
        REQUIRE_FALSE(cfg.setDefaultZoom(7));
        REQUIRE(cfg.snapshot().defaultZoom == 3);

        REQUIRE(cfg.setDefaultZoom(5));
        REQUIRE(cfg.snapshot().defaultZoom == 5);

        // config.json on disk records the value.
        auto cfgText = slurp(dir / "config.json");
        REQUIRE(cfgText.find("defaultZoom") != std::string::npos);
    }

    // A fresh instance over the same dir reads the preference back (sticky),
    // and an unrelated setter must not clobber it.
    {
        UserConfig cfg(dir);
        cfg.start();
        REQUIRE(cfg.snapshot().defaultZoom == 5);
        REQUIRE(cfg.setAutoSaveSram(true));
        REQUIRE(cfg.snapshot().defaultZoom == 5);
    }

    fs::remove_all(dir);
}

TEST_CASE("UserConfig live-reloads when bindings file changes on disk", "[user-config]") {
    auto dir = makeTempDir("live-reload");

    UserConfig cfg(dir);
    cfg.start();

    bool reloaded = false;
    cfg.setOnReload([&] { reloaded = true; });

    // Replace bindings/default.json with a swap-A-and-B variant.
    BindingMapJson swap;
    swap.name = "default";
    swap.keyboard["A"] = {"X", "x"};
    swap.keyboard["B"] = {"Z", "z"};
    writeFile(dir / "bindings" / "default.json", bindingMapToJson(swap));

    const bool sawSwap = waitFor(cfg, [&] {
        auto s = cfg.snapshot();
        auto it = s.bindings.keyboard.find("A");
        return it != s.bindings.keyboard.end() &&
               it->second == std::vector<std::string>{"X", "x"};
    });

    REQUIRE(sawSwap);
    REQUIRE(reloaded);

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::saveProfile writes a new profile file", "[user-config]") {
    auto dir = makeTempDir("save-profile");
    UserConfig cfg(dir);
    cfg.start();

    BindingMapJson b;
    b.keyboard["A"] = {"q"};
    b.gamepad ["A"] = {"y"};
    REQUIRE(cfg.saveProfile("custom", b));

    REQUIRE(fs::exists(dir / "bindings" / "custom.json"));
    auto loaded = cfg.loadProfile("custom");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->keyboard.at("A") == std::vector<std::string>{"q"});
    REQUIRE(loaded->gamepad .at("A") == std::vector<std::string>{"y"});
    REQUIRE(loaded->name == "custom");

    auto snap = cfg.snapshot();
    const auto& list = snap.availableProfiles;
    REQUIRE(std::find(list.begin(), list.end(), std::string("custom")) != list.end());

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::saveProfile rejects invalid names", "[user-config]") {
    auto dir = makeTempDir("save-invalid");
    UserConfig cfg(dir);
    cfg.start();

    BindingMapJson b;
    REQUIRE_FALSE(cfg.saveProfile("",        b));
    REQUIRE_FALSE(cfg.saveProfile("config",  b));     // reserved
    REQUIRE_FALSE(cfg.saveProfile("../etc",  b));     // path traversal
    REQUIRE_FALSE(cfg.saveProfile("a b",     b));     // whitespace
    REQUIRE_FALSE(cfg.saveProfile("a.b",     b));     // dot

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::renameProfile moves the file and updates active references", "[user-config]") {
    auto dir = makeTempDir("rename-profile");
    UserConfig cfg(dir);
    cfg.start();

    BindingMapJson b;
    b.keyboard["A"] = {"j"};
    REQUIRE(cfg.saveProfile("alpha", b));
    REQUIRE(cfg.setActiveKeyboardBindings("alpha"));
    REQUIRE(cfg.snapshot().activeKeyboardBindings == "alpha");

    REQUIRE(cfg.renameProfile("alpha", "beta"));
    REQUIRE_FALSE(fs::exists(dir / "bindings" / "alpha.json"));
    REQUIRE(fs::exists(dir / "bindings" / "beta.json"));

    auto snap = cfg.snapshot();
    REQUIRE(snap.activeKeyboardBindings == "beta");
    REQUIRE(snap.bindings.keyboard.at("A") == std::vector<std::string>{"j"});

    // The renamed file's own `name` field was rewritten to match.
    auto loaded = cfg.loadProfile("beta");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->name == "beta");

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::renameProfile refuses to clobber an existing file", "[user-config]") {
    auto dir = makeTempDir("rename-conflict");
    UserConfig cfg(dir);
    cfg.start();

    BindingMapJson b;
    REQUIRE(cfg.saveProfile("alpha", b));
    REQUIRE(cfg.saveProfile("beta",  b));

    REQUIRE_FALSE(cfg.renameProfile("alpha", "beta"));
    REQUIRE(fs::exists(dir / "bindings" / "alpha.json"));
    REQUIRE(fs::exists(dir / "bindings" / "beta.json"));

    fs::remove_all(dir);
}

TEST_CASE("UserConfig::deleteProfile refuses the active profile", "[user-config]") {
    auto dir = makeTempDir("delete-active");
    UserConfig cfg(dir);
    cfg.start();

    BindingMapJson b;
    REQUIRE(cfg.saveProfile("alt", b));
    REQUIRE(cfg.setActiveKeyboardBindings("alt"));

    // Active keyboard profile — refused.
    REQUIRE_FALSE(cfg.deleteProfile("alt"));
    REQUIRE(fs::exists(dir / "bindings" / "alt.json"));

    // Switch back, then delete succeeds.
    REQUIRE(cfg.setActiveKeyboardBindings("default"));
    REQUIRE(cfg.deleteProfile("alt"));
    REQUIRE_FALSE(fs::exists(dir / "bindings" / "alt.json"));

    fs::remove_all(dir);
}

TEST_CASE("UserConfig keeps previous snapshot AND leaves file alone on malformed JSON", "[user-config]") {
    auto dir = makeTempDir("malformed");

    UserConfig cfg(dir);
    cfg.start();

    const auto bindFile = dir / "bindings" / "default.json";
    const auto before   = cfg.snapshot();

    // User saves a half-typed JSON file.
    const std::string broken = "{\n  \"keyboard\": {";
    writeFile(bindFile, broken);

    // Give efsw a chance to fire; we don't expect snapshot to change.
    waitFor(cfg, [&] { return false; }, 500ms);

    auto after = cfg.snapshot();
    REQUIRE(after.bindings.keyboard == before.bindings.keyboard);
    REQUIRE(after.bindings.gamepad  == before.bindings.gamepad);

    // The broken file is still exactly what the user wrote — we do not
    // attempt to repair or overwrite it.
    REQUIRE(slurp(bindFile) == broken);

    fs::remove_all(dir);
}
