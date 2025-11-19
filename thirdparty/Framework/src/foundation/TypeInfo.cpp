#include "TypeInfo.h"

#include <array>
#include <entt/core/hashed_string.hpp>

using namespace orb;

struct TestFoo {
	int hi;
};

#include "foundation/MetaProperties.h"
/*
const std::array<Property, 1> _properties = {
	Property {
		.hash = entt::hashed_string{ "MyProperty" },
		.name = "MyProperty",
		.value = Range(0.0f, 100.0f)
	}
};

constexpr const std::array<Field, 3> _fields = {
	Field{
		.type = 4,
		.hash = entt::hashed_string{ "orb::Foo1" },
		.name = "orb::Foo1",
		.properties = std::span(_properties)
	},
	Field{
		.type = 3,
		.hash = entt::hashed_string{ "orb::Foo2" },
		.name = "orb::Foo2"
	},
	Field{
		.type = 5,
		.hash = entt::hashed_string{ "orb::Foo3" },
		.name = "orb::Foo3"
	}
};

constexpr const std::array<TypeInfo, 1> _items = {
	TypeInfo {
		.id = 1,
		.hash = entt::hashed_string{ "orb::TestFoo" },
		.name = "orb::TestFoo",
		.fields = std::span(_fields.begin() + 1, _fields.begin() + 2),
		.size = 0
	}
};

const TypeInfo& orb::typeInfo(TypeId type) { 
	assert(type > 0 && type <= _items.size());
	return _items[type - 1]; 
}

template <class T> const TypeInfo& orb::typeInfo<TestFoo>() { return _items[0]; }
*/