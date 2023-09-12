#pragma once

#include <filesystem>
#include <entt/entity/fwd.hpp>

namespace fw {
	struct Stylesheet;
}

namespace fw::CssUtil {
	void setup(entt::registry& reg);

	bool loadStyle(const entt::registry& reg, const std::filesystem::path& path, std::vector<Stylesheet>& styleSheets);
}