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
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::absolute(file, ec);
    if (ec) {
        if (why) *why = "cannot load " + displayPath(file) + ": " + ec.message();
        return nullptr;
    }
#if defined(_WIN32)
    // The DLL's own folder is searched for the DLLs it depends on, then the application's folder, System32 and any
    // folder added with AddDllDirectory: never PATH or the current directory.
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr && why) {
        const DWORD error = GetLastError();  // before anything else can change it
        *why = "cannot load " + displayPath(path) + ": error " + std::to_string(error);
    }
    return reinterpret_cast<void*>(module);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr && why) {
        const char* error = dlerror();
        *why = "cannot load " + displayPath(path) + ": " + (error ? error : "unknown error");
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
