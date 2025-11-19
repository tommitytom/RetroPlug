#include <catch/catch.hpp>

#include "audio/Granular.h"

#include <iostream>

#include "foundation/TypeRegistry.h"
#include "foundation/LuaSerializer.h"

using namespace fw;

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
	entt::any anyValue;
	std::vector<int> intVec;
	std::unordered_map<int, int> intMap;
	std::unordered_map<TypeId, entt::any> typeLookup;
	std::vector<std::string> stringVec;
};

TEST_CASE("Type Registry", "[TypeRegistry]") {
	TypeRegistry reg;

	Foo foo;
	foo.value = 100;
	foo.bar.baz = "bazzzz";

	reg.addType<int>();
	reg.addType<float>();
	reg.addType<std::string>("std::string");
	reg.addType<entt::any>();

	reg.addEnum<EnumTest>();

	reg.addType<Bar>()
		.addField<&Bar::baz>("baz");
	
	reg.addType<Foo>()
		.addField<&Foo::value>("value", Bar())
		.addField<&Foo::f>("f")
		.addField<&Foo::en>("en")
		.addField<&Foo::anyValue>("anyValue");

	const TypeInfo& fooType = reg.getTypeInfo<Foo>();

	fooType.setField<int>(foo, "value", 2000);

	foo.value = 100;
}

struct PropTest {
	int val = 0;
};


TEST_CASE("Lua serialization", "[LuaSerializer]") {
	TypeRegistry reg;
	reg.addCommonTypes();
	reg.addType<TypeId>();
	reg.addType<entt::any>();
	reg.addType<std::vector<int>>();
	reg.addType<std::vector<std::string>>();
	reg.addType<std::unordered_map<int, int>>();
	reg.addType<std::unordered_map<TypeId, entt::any>>();
	reg.addEnum<EnumTest>();

	reg.addType<Bar>()
		.addField<&Bar::baz>("baz");

	reg.addType<Foo>()
		.addField<&Foo::value>("value")
		.addField<&Foo::f>("f")
		.addField<&Foo::bar>("bar")
		.addField<&Foo::en>("en")
		.addField<&Foo::anyValue>("anyValue")
		.addField<&Foo::intVec>("intVec")
		.addField<&Foo::intMap>("intMap")
		.addField<&Foo::typeLookup>("typeLookup")
		.addField<&Foo::stringVec>("stringVec")
		;


	Foo foo;
	foo.value = 100;
	foo.anyValue = std::string("shoit");
	foo.intMap[0] = 0;
	foo.intMap[2] = 1;
	foo.intMap[4] = 2;
	foo.intVec.push_back(1337);
	foo.intVec.push_back(80085);
	foo.stringVec.push_back("hello");
	foo.stringVec.push_back("world!");
	foo.typeLookup[getTypeId<std::string>()] = std::string("string!");
	foo.typeLookup[getTypeId<f32>()] = 42.0f;


	SECTION("Serialize reflected struct to Lua string") {
		std::string str = LuaSerializer::serializeToString(reg, entt::forward_as_any(foo));
		spdlog::info(str);

		Foo fooTarget;
		bool ok = LuaSerializer::deserializeFromString(reg, str, fooTarget);
		
		REQUIRE(ok);
		REQUIRE(fooTarget.value == 100);
		//REQUIRE(fooTarget == foo);
	}

	SECTION("Deserialize reflected struct from Lua string") {
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

	//REQUIRE(foo.value == 9999);
	//REQUIRE(foo.bar.baz == "weeeeeee");
}
