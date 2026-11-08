#include "system/mesen/MesenGlobalInit.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "Core/Shared/MessageManager.h"
#include "Utilities/FolderUtilities.h"

#include "util/ProcessId.hpp"

namespace {

// The live home folder, for the atexit sweep below. Points at the function-local static in
// mesenHomeFolder(), which outlives the handler: the handler is registered after that string finishes
// constructing, and atexit handlers interleave with static destruction in reverse completion order.
const std::string* g_home = nullptr;

// Drop every child of the scratch root that no live process owns. Two kinds qualify: a pid-named
// directory whose pid is gone, and anything NOT pid-named at all - the latter is the pre-pid layout,
// where `staged/` accumulated one directory per system id ever constructed and was never cleaned.
//
// So: an all-digits name survives exactly as long as its pid does, and any other name goes. Conservative
// where it counts, since this deletes - processAlive answers "alive" for anything it cannot prove gone,
// including a pid owned by another user. Errors are ignored throughout: a sweep that cannot run is not a
// reason to fail to start a core. Our OWN pid is skipped here (we are alive), so the caller still clears
// a directory left by a predecessor that held it.
void sweepAbandoned(const std::filesystem::path& base) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(base, ec)) return;

    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const bool numeric = !name.empty() && name.find_first_not_of("0123456789") == std::string::npos;

        // 18 digits is past any pid and short of overflowing the parse; a longer run is not a name we
        // wrote, so it falls through to removal with the pre-pid leftovers.
        if (numeric && name.size() <= 18 && rp::processAlive(std::stoll(name))) continue;

        std::error_code rm;
        fs::remove_all(it->path(), rm);
    }
}

} // namespace

void mesenGlobalInit() {
    (void) mesenHomeFolder();
}

// Mesen reads/writes config files relative to a "home folder"; point it at /tmp so any incidental writes
// don't pollute the user's HOME.
//
// PER PROCESS, and that is load-bearing rather than tidy. The folder is also the root the two staging
// paths hang off - MesenSmsSystem::stageRom writes `<home>/staged/<system id>/<rom stem>.sms` (Mesen's
// SMS/GG model selection and battery stem both come from the FILENAME, and Reset() re-reads the file, so
// the ROM has to exist on disk under its real name), and MesenGbaSystem copies the BIOS to the fixed
// `<home>/Firmware/gba_bios.bin`. With a machine-fixed root, two host processes running the same test
// with the same slot id write - and on teardown DELETE - the same file. The native test runner starts
// `nproc/2` host processes at once, and most of them construct system id 1.
//
// The symptom was a test that flaked only under the full parallel suite, in two flavours: "Could not load
// file" when the ROM had been unlinked mid-load, and a cart that loaded but never booted (work RAM staying
// zeroed) when it read a ROM another process was still writing. The emulation itself is deterministic -
// the same test repeated alone gives identical audio every time - so the shared path was the whole of the
// nondeterminism.
//
// The pid segment is the same fix NesEverdriveFifo already applies to its SD-card scratch dir, for the
// same reason. Removed at exit, and swept on the way in for owners that are gone - a host that is KILLED
// never runs its atexit, which is every Reaper the test suite starts, so the sweep is what keeps the root
// from growing by a directory per run.
const std::string& mesenHomeFolder() {
    static const std::string home = [] {
        namespace fs = std::filesystem;
        std::error_code ec;

        const fs::path base("/tmp/retroplug-mesen");
        sweepAbandoned(base);

        const fs::path dir = base / std::to_string(rp::currentProcessId());
        fs::remove_all(dir, ec); // a previous run that crashed holding this pid
        fs::create_directories(dir, ec);

        std::string h = dir.string();
        FolderUtilities::SetHomeFolder(h);
        MessageManager::SetOptions(false, true);
        return h;
    }();

    static const bool registered = [] {
        g_home = &home;
        std::atexit([] {
            if (!g_home) return;
            std::error_code ec;
            std::filesystem::remove_all(*g_home, ec);
        });
        return true;
    }();
    (void) registered;

    return home;
}
