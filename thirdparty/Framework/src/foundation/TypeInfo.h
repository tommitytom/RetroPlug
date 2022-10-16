#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <entt/core/any.hpp>

#include "foundation/Types.h"

namespace fw {
	using TypeHash = uint32;
	using TypeId = uint32;
	const TypeId INVALID_TYPE = 0;

	struct Property {
		TypeHash hash = 0;
		std::string_view name;
		entt::any value;
	};
	
	struct Field {
		TypeId type = INVALID_TYPE;
		TypeHash hash = 0;
		std::string_view name;
		std::span<const Property> properties;
	};

	struct TypeInfo {
		TypeId id = INVALID_TYPE;
		TypeHash hash = 0;
		std::string_view name;
		std::span<const Field> fields;
		std::span<const Property> properties;
		size_t size = 0;
	};

	template <typename T>
	const TypeInfo& typeInfo();

	const TypeInfo& typeInfo(TypeId type);
}