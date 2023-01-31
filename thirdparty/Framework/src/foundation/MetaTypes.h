#pragma once

#include <entt/core/any.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>

#include "foundation/Types.h"

namespace fw {
	enum class TypeId : uint32 {};
	enum class NameHash : uint32 {};

	const TypeId INVALID_TYPE_ID = entt::null;
	const NameHash INVALID_NAME_HASH = entt::null;

	template<typename, typename = void>
	struct is_dynamic_sequence_container : std::false_type {};

	template<typename Type>
	struct is_dynamic_sequence_container<Type, std::void_t<decltype(&Type::reserve)>> : std::true_type {};

	template<typename, typename = void>
	struct is_key_only_meta_associative_container : std::true_type {};

	template<typename Type>
	struct is_key_only_meta_associative_container<Type, std::void_t<typename Type::mapped_type>> : std::false_type {};

	template <typename T>
	TypeId getTypeId() {
		return TypeId{ entt::type_id<T>().index() };
	}

	static TypeId getTypeId(const entt::any& value) {
		return TypeId{ value.type().index() };
	}
}
