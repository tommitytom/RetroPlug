#pragma once

#if defined(WIN32) || (defined(__cplusplus) && __cplusplus >= 201703L && defined(__has_include) && __has_include(<filesystem>))
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <thirdparty/ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
#endif
