#pragma once

#include "foundation/TypeRegistry.h"
#include "core/RetroPlugConfig.h"

namespace rp::ConfigSerializer {
	std::string serialize(const fw::TypeRegistry& typeRegistry, const RetroPlugConfig& config);

	bool serialize(const fw::TypeRegistry& typeRegistry, std::string_view path, const RetroPlugConfig& config);

	bool deserializeFromMemory(const fw::TypeRegistry& typeRegistry, std::string_view fileData, RetroPlugConfig& config);

	bool deserializeFromFile(const fw::TypeRegistry& typeRegistry, std::string_view path, RetroPlugConfig& config);
}
