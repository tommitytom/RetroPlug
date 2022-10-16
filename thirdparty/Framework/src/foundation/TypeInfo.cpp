#include "TypeInfo.h"

#include <array>
#include <entt/core/hashed_string.hpp>

using namespace fw;

struct TestFoo {
	int hi;
};

#include "foundation/MetaProperties.h"

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
		.hash = entt::hashed_string{ "fw::Foo1" },
		.name = "fw::Foo1",
		.properties = std::span(_properties)
	},
	Field{
		.type = 3,
		.hash = entt::hashed_string{ "fw::Foo2" },
		.name = "fw::Foo2"
	},
	Field{
		.type = 5,
		.hash = entt::hashed_string{ "fw::Foo3" },
		.name = "fw::Foo3"
	}
};

constexpr const std::array<TypeInfo, 1> _items = {
	TypeInfo {
		.id = 1,
		.hash = entt::hashed_string{ "fw::TestFoo" },
		.name = "fw::TestFoo",
		.fields = std::span(_fields.begin() + 1, _fields.begin() + 2),
		.size = 0
	}
};

const TypeInfo& fw::typeInfo(TypeId type) { 
	assert(type > 0 && type <= _items.size());
	return _items[type - 1]; 
}

template <class T> const TypeInfo& fw::typeInfo<TestFoo>() { return _items[0]; }
