#pragma once

#include "foundation/DataBuffer.h"
#include "foundation/TypeRegistry.h"
#include <sol/sol.hpp>

namespace orb::LuaSerializer {
	std::string serializeToString(const orb::TypeRegistry& registry, sol::state& lua, const entt::any& obj);

	std::string serializeToString(const orb::TypeRegistry& registry, const entt::any& obj);

	sol::object serializeToObject(const orb::TypeRegistry& registry, sol::state& lua, const entt::any& obj);

	bool deserialize(const orb::TypeRegistry& registry, const sol::object& source, TypeInstance target);

	bool deserializeFromString(const orb::TypeRegistry& registry, std::string_view source, TypeInstance target);

	bool deserializeFromBuffer(const orb::TypeRegistry& registry, const orb::Uint8Buffer& source, TypeInstance target);
}
