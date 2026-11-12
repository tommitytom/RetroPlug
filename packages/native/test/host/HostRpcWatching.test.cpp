// HostRpcService's watcher gate (spec/07): the service forwards its two watcher calls to the watcher a host hands it,
// and is inert until one is handed over, which is what the test host and the CLI (which watch nothing) rely on. The
// watcher here is a fake, so the case pins the gate itself; the real one is efsw's, and NativeFileWatcher's own cases
// in retroplug-watcher-test hold that, in a binary that links the watcher library alone.
//
// It lives in this binary because the service is a backend object and this is a product binary that already links the
// backend and builds a host (EngineInvoker.test.cpp, beside it); a binary of its own would be a second engine carrier
// for three assertions.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "host/rpc/FileWatcher.hpp"
#include "host/rpc/HostRpcService.hpp"

namespace {

// What a fake was asked to do, held apart from the fake itself: the case reads it AFTER handing the fake over, and if
// the service ever dropped that watcher the object would be gone, so the counts live in memory the test owns and a
// regression fails an assertion instead of touching freed memory.
struct WatcherCalls {
    std::vector<std::string> pending;
    std::vector<std::string> roms;
    int drains = 0;
    int romSets = 0;
};

// Counts the calls it is asked to forward, and hands back what it was told it had seen: enough to say which watcher a
// call reached, and that a second one never gets one.
class FakeFileWatcher : public FileWatcher {
public:
    explicit FakeFileWatcher(std::shared_ptr<WatcherCalls> calls) : _calls(std::move(calls)) { ++live; }
    ~FakeFileWatcher() override { --live; }

    std::vector<std::string> drainChangedPaths() override {
        ++_calls->drains;
        std::vector<std::string> out = _calls->pending;
        _calls->pending.clear();
        return out;
    }
    void setWatchedRoms(const std::vector<std::string>& paths) override {
        ++_calls->romSets;
        _calls->roms = paths;
    }

    // How many fakes are alive, so a watcher the service refuses is seen to be freed at once rather than served.
    static inline int live = 0;

private:
    std::shared_ptr<WatcherCalls> _calls;
};

}  // namespace

TEST_CASE("HostRpcService forwards to the one watcher it was handed", "[watcher]") {
    HostRpcService svc;

    // Inert until a watcher is handed over.
    CHECK(svc.drainChangedPaths().empty());
    CHECK_FALSE(svc.setWatchedRoms({"a.gb"}));

    // Handing none is the same as never enabling watching: the opt-in call may be made with nothing to opt in with.
    svc.enableWatching(nullptr);
    CHECK(svc.drainChangedPaths().empty());
    CHECK_FALSE(svc.setWatchedRoms({"a.gb"}));

    auto firstCalls = std::make_shared<WatcherCalls>();
    firstCalls->pending = {"a.gb"};
    svc.enableWatching(std::make_unique<FakeFileWatcher>(firstCalls));

    CHECK(svc.setWatchedRoms({"a.gb"}));
    CHECK(firstCalls->roms == std::vector<std::string>{"a.gb"});
    CHECK(svc.drainChangedPaths() == std::vector<std::string>{"a.gb"});
    CHECK(svc.drainChangedPaths().empty());
    CHECK(firstCalls->drains == 2);

    // A second watcher is dropped: the first is the service's, so enabling watching twice is idempotent (and the
    // dropped one is freed at once, never handed a call). Both fakes' counts outlive their objects, which is what makes
    // "the first still serves, the second never did" an assertion rather than a read of whatever the service kept.
    const int liveBefore = FakeFileWatcher::live;
    auto secondCalls = std::make_shared<WatcherCalls>();
    svc.enableWatching(std::make_unique<FakeFileWatcher>(secondCalls));
    CHECK(FakeFileWatcher::live == liveBefore);
    CHECK(svc.setWatchedRoms({"b.gb"}));
    CHECK(firstCalls->roms == std::vector<std::string>{"b.gb"});
    CHECK(firstCalls->romSets == 2);
    CHECK(secondCalls->romSets == 0);
    CHECK(secondCalls->drains == 0);
}

TEST_CASE("fileAgeMs measures a file's age on the file clock, and has none where there is no file", "[host]") {
    // What a UI render's snapshot is aged by when a crash leaves it: the sweep TS runs at startup (spec/07) deletes
    // the ones a day old, and asks here rather than comparing a system clock, which would need C++20's clock_cast
    // between the file clock and the system clock, an operation libc++ and MSVC do not agree on.
    namespace fs = std::filesystem;
    std::error_code    ec;
    const fs::path     dir = fs::temp_directory_path(ec) / "rp-hostrpc-file-age";
    fs::remove_all(dir, ec);
    REQUIRE(fs::create_directories(dir, ec));
    REQUIRE_FALSE(ec);

    const fs::path file = dir / "written.txt";
    {
        std::ofstream out(file);
        out << "a snapshot";
    }
    HostRpcService svc;
    const auto     age = svc.fileAgeMs(file.string());
    REQUIRE(age.has_value());
    CHECK(*age >= 0.0);          // a file written now is not from the future
    CHECK(*age < 60000.0);       // and is younger than the minute this case could take
    // A directory ages like a file (the sweep skips them by the trailing '/' listDir marks them with, not here).
    CHECK(svc.fileAgeMs(dir.string()).has_value());
    // Nothing to age is nothing, not zero: a missing file is not one the sweep may delete.
    CHECK_FALSE(svc.fileAgeMs((dir / "absent.txt").string()).has_value());
    fs::remove_all(dir, ec);
}
