#pragma once

#include <string>
#include <vector>

#include "core/FileManager.h"
#include "core/Project.h"

namespace rp::LoaderUtil {
	bool handleLoad(const std::vector<std::string>& files, FileManager& fileManager, Project& project);
}
