// Guards the GBA BIOS install against Mesen's process-global firmware folder.
//
// FirmwareHelper hardcodes the filename `gba_bios.bin` and looks for it under
// FolderUtilities::GetFirmwareFolder(), which is a PROCESS-GLOBAL static. That is fine for an
// application running one emulator and wrong for a host running several: a DAW loads a plugin
// instance per track, and each background render spins its own host. Installing a user's BIOS into
// the shared folder makes "install it, then load it" a two-step transaction over global state - any
// system activating in between replaces the file, and the one mid-activation boots the wrong BIOS.
//
// What is asserted here is the structural property, because that is what is deterministic: each
// system installs into a directory no other system writes. The race itself needs two activations
// interleaved on different threads, which a unit test cannot pin down; removing the shared step is
// what makes it unreachable. Sequential activation was always safe and still is - the last case
// checks the per-instance folders did not break it.
//
// The ROMs and BIOSes are synthetic. Nothing is executed: the reads go through the debugger's
// side-effect-free path. A real GBA BIOS is copyrighted and not in the tree, and would say nothing
// extra - Mesen accepts any 0x4000-byte file, and "which of the two did this emulator load" is the
// whole question.
//
// Run via `pnpm test:plugin`.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenGlobalInit.hpp"

namespace {

constexpr double        kSampleRate   = 48000.0;
constexpr std::uint32_t kBiosSize     = 0x4000; // the only size Mesen accepts for gba_bios.bin
constexpr std::uint32_t kBiosBaseAddr = 0x0000; // the BIOS sits at the bottom of the ARM7 map

// A scratch directory that cleans up after itself, so a failing case cannot leave a stray BIOS behind
// for a later run to pick up.
struct ScratchDir {
    std::filesystem::path path;
    ScratchDir() {
        path = std::filesystem::temp_directory_path() /
               ("rp-gba-fw-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(path);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// A BIOS-shaped file whose every byte is `fill`, so one byte of a read identifies which file it was.
std::string writeBios(const std::filesystem::path& dir, const char* name, std::uint8_t fill) {
    const std::filesystem::path p = dir / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const std::vector<char> bytes(kBiosSize, static_cast<char>(fill));
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p.string();
}

// The smallest thing GbaConsole::LoadRom accepts: it rejects anything under 0xC0 bytes and then reads
// the title/game/maker codes out of the header, which zeroes satisfy.
std::vector<std::uint8_t> stubRom() { return std::vector<std::uint8_t>(1024, 0x00); }

std::unique_ptr<MesenGbaSystem> build(SystemId id, const std::string& biosPath) {
    MesenGbaConfig cfg;
    cfg.biosPath = biosPath;
    auto sys = std::make_unique<MesenGbaSystem>(id, cfg, stubRom());
    sys->onActivate(kSampleRate);
    return sys;
}

// How many gba_bios.bin files this process has installed. One per live GBA system is the invariant;
// one in total means they are sharing, and whichever wrote last owns what everyone loads.
std::size_t installedBiosCount() {
    std::size_t n = 0;
    std::error_code ec;
    const std::filesystem::path home(mesenHomeFolder());
    for (std::filesystem::recursive_directory_iterator it(home, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().filename() == "gba_bios.bin") ++n;
    return n;
}

} // namespace

TEST_CASE("each GBA system installs its BIOS where no other system can reach it", "[audio][gba][firmware]") {
    ScratchDir dir;
    const std::string biosA = writeBios(dir.path, "a.bin", 0xA1);
    const std::string biosB = writeBios(dir.path, "b.bin", 0xB2);

    // Count rather than assume an empty home: this binary runs every other case in the same process,
    // against the same per-process Mesen home folder.
    const std::size_t before = installedBiosCount();

    // The same id on purpose - ids are allocated per Project, so two instances really do both use 1.
    auto a = build(1, biosA);
    REQUIRE(installedBiosCount() == before + 1);

    auto b = build(1, biosB);
    CHECK(installedBiosCount() == before + 2); // one shared folder would still show a single file

    // Each emulator booted the BIOS it was given, not whichever was installed last.
    CHECK(a->readCpuByte(kBiosBaseAddr) == std::optional<std::uint8_t>{0xA1});
    CHECK(b->readCpuByte(kBiosBaseAddr) == std::optional<std::uint8_t>{0xB2});

    // Teardown takes its own copy and only its own.
    b.reset();
    CHECK(installedBiosCount() == before + 1);
    CHECK(a->readCpuByte(kBiosBaseAddr) == std::optional<std::uint8_t>{0xA1});

    a.reset();
    CHECK(installedBiosCount() == before);
}
