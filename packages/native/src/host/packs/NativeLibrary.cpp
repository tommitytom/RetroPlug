#include "host/packs/NativeLibrary.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "host/packs/PackDirs.hpp"

namespace rp::host::packs {

void* openLibrary(const std::filesystem::path& file, std::string* why) {
#if defined(_WIN32)
    // The pack's own folder is searched for the DLLs it depends on, then the system's; never the current directory.
    HMODULE module = LoadLibraryExW(file.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr && why)
        *why = "cannot load " + displayPath(file) + ": error " + std::to_string(GetLastError());
    return reinterpret_cast<void*>(module);
#else
    void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr && why) {
        const char* error = dlerror();
        *why = "cannot load " + displayPath(file) + ": " + (error ? error : "unknown error");
    }
    return handle;
#endif
}

void* librarySymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

}  // namespace rp::host::packs
