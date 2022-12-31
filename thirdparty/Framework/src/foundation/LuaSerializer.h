#pragma once

#include "foundation/TypeRegistry.h"
#include <sol/sol.hpp>

namespace fw::LuaSerializer {
	bool serialize(const fw::TypeRegistry& registry, const entt::any& obj, sol::table& target);

	bool deserialize(const fw::TypeRegistry& registry, const sol::object& source, TypeInstance target);
}
