#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace rp::FsUtil {
	std::vector<std::byte> readFile(const fs::path& path);
}
