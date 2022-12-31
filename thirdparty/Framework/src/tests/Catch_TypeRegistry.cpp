#include <catch/catch.hpp>

#include "audio/Granular.h"

#include <iostream>

using namespace fw;

#include "foundation/TypeRegistry.h"
#include "foundation/LuaSerializer.h"

struct Bar {
	std::string baz;
};

enum class EnumTest {
	One,
	Two,
	Three
};

struct Foo {
	int value = 0;
	float f = 1337.0f;
	Bar bar;
	EnumTest en = EnumTest::One;
};

TEST_CASE("Type Registry", "[TypeRegistry]") {
	TypeRegistry reg;

	Foo foo;
	foo.value = 100;
	foo.bar.baz = "bazzzz";

	reg.addType<int>();
	reg.addType<float>();
	reg.addType<std::string>("std::string");

	reg.addEnum<EnumTest>();

	reg.addType<Bar>()
		.addField<&Bar::baz>("baz");
	
	reg.addType<Foo>()
		.addField<&Foo::value>("value", Bar())
		.addField<&Foo::f>("f")
		.addField<&Foo::en>("en");

	const TypeInfo& fooType = reg.getTypeInfo<Foo>();

	fooType.setField<int>(foo, "value", 2000);

	foo.value = 100;
}

struct PropTest {
	int val = 0;
};



TEST_CASE("Lua Deserializer", "[TypeRegistry]") {
	TypeRegistry reg;

	Foo foo;
	foo.value = 100;

	reg.addCommonTypes();

	reg.addType<Bar>()
		.addField<&Bar::baz>("baz");
	
	reg.addType<Foo>()
		.addField<&Foo::value>("value")
		.addField<&Foo::f>("f")
		.addField<&Foo::bar>("bar");	

	reg.addType<EnumTest>()
		.addField<EnumTest::One>("one");

	sol::state lua;
	lua.script(R"(
		obj = {
			value = 9999,
			f = 134.1,
			bar = {
				baz = "weeeeeee"
			}	
		}
	)");

	auto v = lua["obj"];

	LuaSerializer::deserialize(reg, v, foo);

	REQUIRE(foo.value == 9999);
	REQUIRE(foo.bar.baz == "weeeeeee");
}
