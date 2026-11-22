// state.json and the version locks (docs/downloadable-packs-loading-design.md section 1): the state round-trips and is
// replaced atomically, a newer one is left alone, concurrent writers each land, a version folder is in use while a
// process holds it, and a launch settles pending updates and removals. Every case runs on a TempDir.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "PackTestSupport.hpp"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackState.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::TempDir;

namespace {

void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The file's identity, which a rewrite (a temporary renamed over it) changes and an untouched file keeps. A timestamp
// would not do: it can match across two writes inside the file system's granularity.
#if defined(_WIN32)
std::string fileIdentity(const fs::path& file) {
    HANDLE h = CreateFileW(file.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    FILE_ID_INFO id{};
    const bool ok = GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof id) != 0;
    CloseHandle(h);
    return ok ? std::string(reinterpret_cast<const char*>(&id), sizeof id) : std::string();
}
#else
std::string fileIdentity(const fs::path& file) {
    struct stat st {};
    return ::stat(file.c_str(), &st) == 0 ? std::to_string(st.st_ino) : std::string();
}
#endif

}  // namespace

TEST_CASE("state.json round-trips, is replaced atomically, and a newer one is left alone", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::string why;
    // No file is an empty state.
    auto empty = readPackState(root, &why);
    REQUIRE(empty);
    CHECK(empty->packs.empty());

    PackState state;
    state.packs.push_back({"mesen", "1.2.0", "1.3.0", false});
    state.packs.push_back({"risa", "2.3.0", "", true});
    REQUIRE(writePackState(root, state, &why));
    const auto back = readPackState(root, &why);
    INFO(why);
    REQUIRE(back);
    REQUIRE(back->packs.size() == 2);
    CHECK(back->find("mesen")->pending == "1.3.0");
    CHECK(back->find("risa")->remove);
    // Nothing but state.json and the lock every access holds is left beside it: the temporary file was renamed over it.
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(root)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"state.json", "state.lock"});

    // A newer RetroPlug's file is refused and never overwritten.
    const std::string newer = R"({"version": 99, "packs": []})";
    writeText(root / "state.json", newer);
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("newer RetroPlug"));
    CHECK_FALSE(updatePackState(root, [](PackState&) { return true; }, &why));
    CHECK(readText(root / "state.json") == newer);
    // Read for its version first: a newer schema that does not parse as this one is still newer, not malformed.
    writeText(root / "state.json", R"({"version": 2, "packs": "a newer shape"})");
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("newer RetroPlug"));

    // And a malformed entry is refused, naming it.
    writeText(root / "state.json", R"({"version": 1, "packs": [{"id": "Bad Id", "active": "1.0.0"}]})");
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("is not a pack id"));
}

TEST_CASE("updatePackState serialises writers: every change lands", "[packs-folders]") {
    // Each thread takes the exclusive lock on its own open of state.lock, as a second process would, so none of the
    // read-modify-writes overwrites another's. Catch's assertions are not thread safe, so each thread records its
    // outcomes and the checks run here.
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    constexpr int kThreads = 8;
    constexpr int kEach = 25;
    std::vector<std::string> failures(kThreads);
    std::vector<std::thread> writers;
    for (int i = 0; i < kThreads; ++i)
        writers.emplace_back([&root, &failures, i] {
            for (int j = 0; j < kEach; ++j) {
                std::string why;
                const std::string id = "pack-" + std::to_string(i) + "-" + std::to_string(j);
                const bool ok = updatePackState(
                    root,
                    [&id](PackState& s) {
                        s.packs.push_back({id, "1.0.0", "", false});
                        return true;
                    },
                    &why);
                if (!ok || !why.empty()) failures[static_cast<std::size_t>(i)] += id + ": " + why + "; ";
            }
        });
    for (auto& t : writers) t.join();
    for (const auto& f : failures) CHECK(f.empty());
    std::string why;
    const auto state = readPackState(root, &why);
    REQUIRE(state);
    CHECK(state->packs.size() == static_cast<std::size_t>(kThreads * kEach));
}

TEST_CASE("a version folder is in use while a process holds it", "[packs-folders]") {
    TempDir tmp;
    const fs::path version = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    CHECK_FALSE(versionInUse(version));  // no lock file yet: nobody has loaded it
    std::string why;
    {
        auto held = holdVersion(version, &why);
        INFO(why);
        REQUIRE(held);
        CHECK(versionInUse(version));
        auto second = holdVersion(version, &why);  // a second process loading the same version shares it
        CHECK(second);
    }
    CHECK_FALSE(versionInUse(version));
}

TEST_CASE("a launch settles state.json: pending versions switch, removals go, unheld old folders are deleted",
          "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path oldA = rp::host::packs::test::install(root, "packfixture", "1.0.0");
    const fs::path newA = rp::host::packs::test::install(root, "packfixture", "1.1.0");
    const fs::path oldB = rp::host::packs::test::install(root, "packfixture-dependent", "1.0.0");
    const fs::path heldC = rp::host::packs::test::install(root, "packfixture-clone", "1.0.0");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "1.1.0", false});
    state.packs.push_back({"packfixture-dependent", "1.0.0", "", true});
    state.packs.push_back({"packfixture-clone", "1.0.0", "", true});
    REQUIRE(writePackState(root, state, &why));
    // Another process still runs the clone's version.
    auto held = holdVersion(heldC, &why);
    REQUIRE(held);

    REQUIRE(settlePackState(root, &why));
    const auto settled = readPackState(root, &why);
    REQUIRE(settled);
    REQUIRE(settled->packs.size() == 1);
    CHECK(settled->packs[0].id == "packfixture");
    CHECK(settled->packs[0].active == "1.1.0");
    CHECK(settled->packs[0].pending.empty());
    CHECK_FALSE(fs::exists(oldA));
    CHECK(fs::exists(newA));
    CHECK_FALSE(fs::exists(oldB.parent_path()));  // the removed pack's folder, emptied, goes too
    CHECK(fs::exists(heldC));                    // held: left for a later launch

    held.reset();
    REQUIRE(settlePackState(root, &why));
    CHECK_FALSE(fs::exists(heldC));
}

TEST_CASE("a lock on a file that was replaced while it was taken is refused", "[packs-folders]") {
    // The settle deletes a version folder while it holds the exclusive lock; a hold that opened the lock file before
    // that must not "hold" a file that is no longer the folder's.
    TempDir tmp;
    const fs::path version = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    const fs::path lockFile = version / kVersionLockFile;
    std::string why;
    FileLock::Failure failure = FileLock::Failure::None;
    auto held = FileLock::acquire(
        lockFile, FileLock::Mode::Shared, true, &why, &failure, [&] {
            std::error_code ec;
            fs::remove(lockFile, ec);
            std::ofstream(lockFile) << "";
        });
    CHECK_FALSE(held);
    CHECK(failure == FileLock::Failure::Replaced);
    CHECK_THAT(why, ContainsSubstring("being deleted"));
    // The next hold, on the file now there, is ordinary.
    CHECK(holdVersion(version, &why));
}

TEST_CASE("a launch with no state.json sweeps nothing, and a settle that changes nothing writes nothing",
          "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path version = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(settlePackState(root, &why));
    CHECK(fs::exists(version));
    CHECK_FALSE(fs::exists(root / "state.json"));  // nothing recorded, nothing written

    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    const auto before = fileIdentity(root / "state.json");
    // Leftovers a crash made: a writer's temporary, and a deletion's trash.
    writeText(root / "state.json.tmp-1-0", "x");
    writeText(version.parent_path() / ".0.9.0.deleting-1-0" / "pack.json", "{}");
    REQUIRE(settlePackState(root, &why));
    CHECK(fileIdentity(root / "state.json") == before);  // a rewrite renames a new file over it: a new inode
    CHECK(fs::exists(version));
    CHECK_FALSE(fs::exists(root / "state.json.tmp-1-0"));
    CHECK_FALSE(fs::exists(version.parent_path() / ".0.9.0.deleting-1-0"));
}

TEST_CASE("state.json version 0 is malformed", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    writeText(root / "state.json", R"({"version": 0, "packs": []})");
    std::string why;
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("is not a version"));
}

#if !defined(_WIN32)
TEST_CASE("updatePackState serialises writers across processes", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    constexpr int kChildren = 4;
    constexpr int kEach = 50;
    // fork in a process with other threads running copies only the calling one, and a lock or allocator another held is
    // held forever in the child: this case relies on no other thread existing here (the sanitizer legs run it too).
    std::vector<pid_t> children;
    for (int c = 0; c < kChildren; ++c) {
        const pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            // The child only calls what it needs and leaves with _exit, never touching the parent's Catch state.
            int bad = 0;
            for (int j = 0; j < kEach; ++j) {
                std::string why;
                const std::string id = "child-" + std::to_string(c) + "-" + std::to_string(j);
                const bool ok = updatePackState(
                    root,
                    [&id](PackState& s) {
                        s.packs.push_back({id, "1.0.0", "", false});
                        return true;
                    },
                    &why);
                if (!ok) ++bad;
            }
            _exit(bad == 0 ? 0 : 1);
        }
        children.push_back(pid);
    }
    for (const pid_t pid : children) {
        int status = 0;
        REQUIRE(waitpid(pid, &status, 0) == pid);
        CHECK((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    }
    std::string why;
    const auto state = readPackState(root, &why);
    INFO(why);
    REQUIRE(state);
    CHECK(state->packs.size() == static_cast<std::size_t>(kChildren * kEach));
}

TEST_CASE("a process killed while it holds a version leaves no lock behind", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path version = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{}, &why));
    int ready[2];
    REQUIRE(pipe(ready) == 0);
    // fork carries the same single-threaded assumption as the case above.
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    // Whatever fails below, the child must not outlive the case, or it would hold the lock and a pipe for good.
    struct Reap {
        pid_t pid;
        bool  done = false;  // set once the case has reaped the child itself, so a recycled pid is never signalled
        ~Reap() {
            if (done) return;
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
        }
    } reap{pid};
    if (pid == 0) {
        auto held = holdVersion(version, nullptr);
        const char byte = held ? 1 : 0;
        (void)!write(ready[1], &byte, 1);
        for (;;) pause();
    }
    char byte = 0;
    REQUIRE(read(ready[0], &byte, 1) == 1);
    REQUIRE(byte == 1);
    CHECK(versionInUse(version));
    REQUIRE(settlePackState(root, &why));
    CHECK(fs::exists(version));  // held by the child, so the settle leaves it
    kill(pid, SIGKILL);
    int status = 0;
    const pid_t waited = waitpid(pid, &status, 0);
    reap.done = waited == pid;
    REQUIRE(waited == pid);
    close(ready[0]);
    close(ready[1]);
    CHECK_FALSE(versionInUse(version));
    REQUIRE(settlePackState(root, &why));
    CHECK_FALSE(fs::exists(version));
}
#endif

#if !defined(_WIN32)
TEST_CASE("a data root this process cannot write is read without a lock", "[packs-folders]") {
    // A read-only profile or a shared install: no state.lock can be made there, and nothing can write state.json
    // there either, so the read goes ahead unheld rather than failing.
    if (geteuid() == 0) SKIP("root writes through any permission, so a read-only folder cannot be made");
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    fs::remove(root / kStateLockFile);
    fs::permissions(root, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);
    const auto state = readPackState(root, &why);
    fs::permissions(root, fs::perms::owner_write, fs::perm_options::add);  // so the TempDir can go
    INFO(why);
    REQUIRE(state);
    CHECK(state->find("packfixture") != nullptr);
    CHECK_FALSE(fs::exists(root / kStateLockFile));
}
#endif
