#pragma once

#include <string>

// The host this translation unit was compiled for, "<os>-<arch>-<compiler><major>", read from the compiler's own
// macros: a CMake variable would name the configure host, so each slice of a universal binary names itself.
//
// The engine's `buildTarget` RPC and the kit baseline's golden both key on this string, so they share one spelling:
// a golden recorded on one host and a check run on another must name the two toolchains the same way to be
// comparable. On Windows the SameBoy core is compiled by clang-cl (cmake/sameboy.cmake), whose version this key does
// not carry; that would matter only if a Windows baseline were ever recorded.
namespace rp {

inline std::string buildTarget() {
#if defined(__linux__)
    const std::string os = "linux";
#elif defined(__APPLE__)
    const std::string os = "darwin";
#elif defined(_WIN32)
    const std::string os = "windows";
#else
    const std::string os = "unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
    const std::string arch = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const std::string arch = "arm64";
#else
    const std::string arch = "unknown";
#endif
#if defined(__clang__) && defined(__apple_build_version__)
    const std::string cc = "appleclang" + std::to_string(__clang_major__);
#elif defined(__clang__)
    const std::string cc = "clang" + std::to_string(__clang_major__);
#elif defined(_MSC_VER)
    const std::string cc = "msvc" + std::to_string(_MSC_VER);  // the full number: MSVC 14.44 and 14.51 differ
#elif defined(__GNUC__)
    const std::string cc = "gnu" + std::to_string(__GNUC__);
#else
    const std::string cc = "unknown";
#endif
    return os + "-" + arch + "-" + cc;
}

} // namespace rp
