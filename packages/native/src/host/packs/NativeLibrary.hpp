#pragma once

#include <filesystem>
#include <string>

// A pack's native library, opened for the life of the process. There is no close: a pack's code is never unloaded
// (docs/downloadable-packs-loading-design.md section 2), and a refused pack's library stays mapped too, since opening
// it already ran its static constructors and whatever they registered (an atexit handler among them) must still find
// its code at exit.
namespace rp::host::packs {

// The library at `file`, or null with the loader's reason in *why. Its symbols stay local to it (RTLD_LOCAL): a pack's
// copies of what it links are its own, and nothing loaded later binds to them.
void* openLibrary(const std::filesystem::path& file, std::string* why);

// The symbol `name` in `library`, or null.
void* librarySymbol(void* library, const char* name);

}  // namespace rp::host::packs
