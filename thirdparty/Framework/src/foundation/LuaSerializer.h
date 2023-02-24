#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/TypeRegistry.h"
#include <sol/sol.hpp>

namespace fw::LuaSerializer {
	std::string serializeToString(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& obj);

	std::string serializeToString(const fw::TypeRegistry& registry, const entt::any& obj);

	sol::object serializeToObject(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& obj);

	bool deserialize(const fw::TypeRegistry& registry, const sol::object& source, TypeInstance target);

	bool deserializeFromString(const fw::TypeRegistry& registry, std::string_view source, TypeInstance target);

	bool deserializeFromBuffer(const fw::TypeRegistry& registry, const fw::Uint8Buffer& source, TypeInstance target);
}
