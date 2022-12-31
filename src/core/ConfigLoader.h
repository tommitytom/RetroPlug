#pragma once

#include <filesystem>
#include "ProjectState.h"
#include "foundation/TypeRegistry.h"

namespace rp::ConfigLoader {
	bool loadConfig(const fw::TypeRegistry& typeRegistry, const std::filesystem::path& path, GlobalConfig& target);
}
