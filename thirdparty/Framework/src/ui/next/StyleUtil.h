#pragma once

#include <entt/entity/fwd.hpp>
#include "DocumentTypes.h"
#include "Stylesheet.h"

namespace fw::StyleUtil {
	void setup(entt::registry& reg);

	void addStyleSheets(entt::registry& reg, std::vector<Stylesheet>&& stylesheets);

	StyleHandle createEmptyStyle(entt::registry& reg, const DomElementHandle element);
	
	void markStyleDirty(entt::registry& reg, const DomElementHandle handle, bool recurse);
	
	void update(entt::registry& reg, f32 dt);
}
