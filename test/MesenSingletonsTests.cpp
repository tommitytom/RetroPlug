// Direct probes of the Mesen-side process-global state that
// `MesenNesSystem::onActivate` reaches into. These are diagnostic — each test
// documents one observable property of one singleton so future changes
// produce a precise failure rather than a vague "something's off when two
// plugins coexist".
//
// Mesen singletons in scope here (the four touched on RetroPlug's NES path):
//   1. FolderUtilities::_homeFolder           (deps/mesen/Utilities/FolderUtilities.cpp:16)
//   2. MessageManager::_osdEnabled            (deps/mesen/Core/Shared/MessageManager.cpp:87)
//      MessageManager::_outputToStdout        (deps/mesen/Core/Shared/MessageManager.cpp:88)
//   3. MessageManager::_messageManager        (deps/mesen/Core/Shared/MessageManager.cpp:89)
//      MessageManager::_log                   (deps/mesen/Core/Shared/MessageManager.cpp:84)
//   4. GameDatabase::_gameDatabase et al      (deps/mesen/Core/NES/GameDatabase.cpp:12-15)
//
// Integration with real MesenNesSystem instances lives in
// MesenMultiInstanceTests.cpp.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "Core/NES/GameDatabase.h"
#include "Core/NES/RomData.h"
#include "Core/Shared/Interfaces/IMessageManager.h"
#include "Core/Shared/MessageManager.h"
#include "Utilities/FolderUtilities.h"

namespace {

// IMessageManager test double. Records every DisplayMessage call so the
// tests can assert which handler received what.
class RecordingHandler : public IMessageManager {
public:
    void DisplayMessage(std::string title, std::string message) override {
        ++count;
        last = title + "|" + message;
    }
    std::size_t count = 0;
    std::string last;
};

// RAII guard: register a handler, unregister it on scope exit. Keeps the
// MessageManager singleton clean for the next test even if the body throws.
struct ScopedHandler {
    explicit ScopedHandler(IMessageManager* h) : handler(h) {
        MessageManager::RegisterMessageManager(handler);
    }
    ~ScopedHandler() {
        MessageManager::UnregisterMessageManager(handler);
    }
    IMessageManager* handler;
};

} // namespace

// ---------------------------------------------------------------------------
// 1. FolderUtilities::SetHomeFolder is last-writer-wins.
// ---------------------------------------------------------------------------
// In practice MesenNesSystem hard-codes "/tmp/retroplug-mesen" so two instances
// write the same value — benign today. The hazard is latent: any future
// per-instance home folder will silently lose to whichever onActivate fires
// last.
TEST_CASE("FolderUtilities::SetHomeFolder is last-writer-wins (singleton)",
          "[MesenSingleton][FolderUtilities]") {
    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen-test-A");
    REQUIRE(FolderUtilities::GetHomeFolder() == "/tmp/retroplug-mesen-test-A");

    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen-test-B");
    REQUIRE(FolderUtilities::GetHomeFolder() == "/tmp/retroplug-mesen-test-B");

    // Restore the shared default so subsequent tests start from the same
    // state MesenNesSystem::onActivate would have left.
    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen");
}

// ---------------------------------------------------------------------------
// 2. FolderUtilities::_homeFolder write is not lock-protected (known race).
// ---------------------------------------------------------------------------
// SetHomeFolder assigns a std::string with no synchronization (see
// deps/mesen/Utilities/FolderUtilities.cpp:24). GetHomeFolder returns by
// value, copying the string while the writer may be reseating its buffer —
// that's UB any sanitizer would flag.
//
// This test stresses the path so we (a) don't crash on the platforms we care
// about and (b) document that ThreadSanitizer / AddressSanitizer should be
// expected to fire here. The reader always observes one of the two posted
// strings; we accept either as a "no torn read" outcome.
TEST_CASE("FolderUtilities concurrent SetHomeFolder stress (known race)",
          "[MesenSingleton][FolderUtilities][!mayfail]") {
    const std::string a = "/tmp/retroplug-mesen-race-A";
    const std::string b = "/tmp/retroplug-mesen-race-B";

    FolderUtilities::SetHomeFolder(a);

    std::atomic<bool> stop{false};
    std::thread w1([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            FolderUtilities::SetHomeFolder(a);
        }
    });
    std::thread w2([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            FolderUtilities::SetHomeFolder(b);
        }
    });

    for (int i = 0; i < 2000; ++i) {
        std::string seen = FolderUtilities::GetHomeFolder();
        // Reader must always see one of the two valid strings. Anything else
        // is a torn read (or, more typically, a sanitizer-detected race).
        REQUIRE((seen == a || seen == b));
    }

    stop.store(true, std::memory_order_relaxed);
    w1.join();
    w2.join();

    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen");
}

// ---------------------------------------------------------------------------
// 3. MessageManager::SetOptions is last-writer-wins, observable via the
//    DisplayMessage routing path.
// ---------------------------------------------------------------------------
// With _osdEnabled=true, DisplayMessage forwards to the registered handler.
// With _osdEnabled=false, DisplayMessage falls back to Log() instead. Each
// MesenNesSystem::onActivate calls SetOptions(false, true) — so a future
// instance that wanted OSD on would be silently overridden.
TEST_CASE("MessageManager::SetOptions is last-writer-wins (singleton)",
          "[MesenSingleton][MessageManager]") {
    MessageManager::ClearLog();
    RecordingHandler handler;
    ScopedHandler guard(&handler);

    MessageManager::SetOptions(/*osdEnabled=*/true, /*outputToStdout=*/false);
    MessageManager::DisplayMessage("t1", "m1");
    REQUIRE(handler.count == 1);
    REQUIRE(handler.last == "t1|m1");

    // The same call MesenNesSystem makes. Now OSD is off; DisplayMessage routes
    // to Log() instead of the handler.
    MessageManager::SetOptions(/*osdEnabled=*/false, /*outputToStdout=*/false);
    MessageManager::DisplayMessage("t2", "m2");
    REQUIRE(handler.count == 1); // unchanged
    REQUIRE(MessageManager::GetLog().find("[t2] m2") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 4. MessageManager::RegisterMessageManager silently drops the second
//    registration (single-slot singleton).
// ---------------------------------------------------------------------------
// RegisterMessageManager only writes the slot if it's currently nullptr (see
// deps/mesen/Core/Shared/MessageManager.cpp:91-97). Two plugin instances both
// trying to attach handlers means only the first wins; the second silently
// receives no notifications. RetroPlug doesn't currently register a handler,
// so this is latent — but it's the most user-visible cross-instance bug if
// the design ever changes.
TEST_CASE("MessageManager::RegisterMessageManager drops the second handler",
          "[MesenSingleton][MessageManager]") {
    MessageManager::ClearLog();
    MessageManager::SetOptions(/*osdEnabled=*/true, /*outputToStdout=*/false);

    RecordingHandler first;
    RecordingHandler second;

    MessageManager::RegisterMessageManager(&first);
    MessageManager::RegisterMessageManager(&second); // silent no-op

    MessageManager::DisplayMessage("title", "body");

    CHECK(first.count == 1);
    CHECK(second.count == 0);

    MessageManager::UnregisterMessageManager(&first);
    // The slot is now nullptr — `second` can finally take it.
    MessageManager::RegisterMessageManager(&second);
    MessageManager::DisplayMessage("again", "body2");

    CHECK(first.count == 1);
    CHECK(second.count == 1);

    MessageManager::UnregisterMessageManager(&second);
}

// ---------------------------------------------------------------------------
// 5. MessageManager::_log is bounded (≤1000 entries) and lock-protected.
// ---------------------------------------------------------------------------
// Both Log() and GetLog() acquire _logLock, so concurrent writes from
// multiple plugin instances are serialized. The 1000-entry cap is process-
// global, so logs from instance A and instance B interleave and the oldest
// half of A's boot log may be evicted by B's. Benign for our use case.
TEST_CASE("MessageManager::Log is thread-safe and bounded at 1000 entries",
          "[MesenSingleton][MessageManager]") {
    MessageManager::ClearLog();
    MessageManager::SetOptions(/*osdEnabled=*/false, /*outputToStdout=*/false);

    constexpr int kThreads        = 8;
    constexpr int kLogsPerThread  = 500;
    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([t] {
            for (int i = 0; i < kLogsPerThread; ++i) {
                MessageManager::Log("t" + std::to_string(t) +
                                    "_msg" + std::to_string(i));
            }
        });
    }
    for (auto& w : writers) w.join();

    std::string log = MessageManager::GetLog();

    std::size_t lineCount = 0;
    for (char c : log) if (c == '\n') ++lineCount;

    // Capped at 1000 even though we wrote 4000.
    REQUIRE(lineCount <= 1000);
    // Survived concurrency without crash or message corruption: every
    // emitted line should start with "t<digit>_msg".
    std::size_t prefixOk = 0;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i] == 't' && i + 2 < log.size() &&
            log[i + 1] >= '0' && log[i + 1] <= '9' && log[i + 2] == '_') {
            ++prefixOk;
        }
    }
    REQUIRE(prefixOk >= lineCount); // every line begins with a valid tag

    MessageManager::ClearLog();
}

// ---------------------------------------------------------------------------
// 6. GameDatabase access is lock-protected under contention.
// ---------------------------------------------------------------------------
// GetiNesHeader lazily calls InitDatabase() (see
// deps/mesen/Core/NES/GameDatabase.cpp), which double-checked-locks on
// `_loadLock` and reads `MesenNesDB.txt` from the home folder exactly once
// per process (the `_initialized` flag). Hammering it from multiple threads
// should be crash-free, with the lookups missing (we don't ship a DB file
// in this dir, so the DB loads empty).
//
// InitDatabase() requires a home folder: FolderUtilities::GetHomeFolder()
// throws "Home folder not specified" when none is set, and that throw inside
// the worker threads would std::terminate the process. Production sets it in
// MesenNesSystem::onActivate; set it here too so the test is order-independent
// rather than relying on an earlier test having seeded the singleton.
TEST_CASE("GameDatabase concurrent lookups don't crash",
          "[MesenSingleton][GameDatabase]") {
    constexpr int kThreads        = 4;
    constexpr int kLookupsPerThread = 200;

    // No MesenNesDB.txt here → InitDatabase() loads an empty DB; every lookup
    // below misses. The point is to stress the lazy-init lock + miss path.
    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen-gamedb-test");

    std::vector<std::thread> readers;
    std::atomic<int> done{0};
    readers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        readers.emplace_back([t, &done] {
            for (int i = 0; i < kLookupsPerThread; ++i) {
                NesHeader hdr{};
                // CRCs deliberately unlikely to be in the DB — we're
                // exercising the lock + miss path, not the hit path.
                std::uint32_t crc =
                    static_cast<std::uint32_t>(t) * 1000u +
                    static_cast<std::uint32_t>(i);
                (void)GameDatabase::GetiNesHeader(crc, hdr);
            }
            done.fetch_add(1);
        });
    }
    for (auto& r : readers) r.join();
    REQUIRE(done.load() == kThreads);
}
