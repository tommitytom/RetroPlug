#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "foundation/Types.h"

namespace fw::ProcessUtil {
	// Runs the specified process.  Blocks the current thread until the process has finished.
	int32 runProcess(const std::string& path, const std::vector<std::string>& args = std::vector<std::string>(), bool silent = false);
}
