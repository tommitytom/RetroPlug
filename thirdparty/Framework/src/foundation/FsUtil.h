#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace fw::FsUtil {
	std::vector<std::byte> readFile(const fs::path& path);
}
