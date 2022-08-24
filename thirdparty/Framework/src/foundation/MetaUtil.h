#pragma once

#include <string_view>
#include <entt/meta/resolve.hpp>

namespace fw::MetaUtil {
	inline std::string_view getTypeName(entt::id_type typeId) {
		entt::meta_type type = entt::resolve(typeId);

		std::string_view typeName = type.info().name();
		if (typeName.starts_with("class ")) {
			return typeName.substr(6);
		}

		if (typeName.starts_with("struct ")) {
			return typeName.substr(7);
		}

		return typeName;
	}
}
