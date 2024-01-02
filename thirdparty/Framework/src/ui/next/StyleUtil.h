#pragma once

#include <entt/entity/registry.hpp>
#include "DocumentTypes.h"
#include "DocumentUtil.h"
#include "Stylesheet.h"
#include "StyleComponents.h"

namespace fw::StyleUtil {
	void setup(entt::registry& reg);

	void addStyleSheets(entt::registry& reg, const std::filesystem::path& path, std::vector<Stylesheet>&& stylesheets);

	StyleHandle createEmptyStyle(entt::registry& reg, const DomElementHandle element);
	
	void markStyleDirty(entt::registry& reg, const DomElementHandle handle, bool recurse);
	
	void update(entt::registry& reg, f32 dt);

	template <typename T>
	const T* findProperty(const entt::registry& reg, DomElementHandle handle, bool inherit) {
		while (handle != entt::null) {
			const T* found = reg.try_get<T>(reg.get<StyleReferences>(handle).current);
			if (found) { return found; }

			if (!inherit) {
				return nullptr;
			}
			
			handle = DocumentUtil::getParent(reg, handle);
		}

		return nullptr;
	}
}
