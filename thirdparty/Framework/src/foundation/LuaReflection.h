#pragma once

#include <array>
#include <stack>
#include <string>
#include <string_view>

#include <refl.hpp>
#include <spdlog/spdlog.h>
#include <lua.hpp>
#include <LuaBridge.h>
#include <magic_enum.hpp>

#include "foundation/ReflUtil.h"
#include "foundation/Object.h"

namespace fw::LuaReflection {
	namespace internal {
		constexpr size_t MAX_NAMESPACES = 4;
		std::string splitTypeName(std::string_view name, std::array<std::string, MAX_NAMESPACES>& namespaces);

		std::string splitTypeName(std::string_view name, std::vector<std::string>& namespaces);
		
		template <typename T>
		std::string_view getTypeName() {
			std::string_view name = entt::type_name<T>::value();
			size_t idx = name.find_last_of(" ");
			if (idx != std::string_view::npos) {
				return name.substr(idx + 1);
			}

			return name;
		}
	}

	template <typename Func>
	struct ctor_ptr : refl::attr::usage::type {
		using RT = std::remove_pointer_t<decltype(std::declval<Func>()(nullptr, nullptr))>;
		static_assert(std::is_same_v<Func, RT* (*)(void*, lua_State*)>, "Func must be a function pointer with signature RT*(*)(void*, lua_State*)");

		Func func;
		constexpr ctor_ptr(Func func) : func(func) {}
	};
	
	template <typename T>
	void addEnum(lua_State* l) {
		std::vector<std::string> namespaces;
		std::string enumName = internal::splitTypeName(internal::getTypeName<T>(), namespaces);
		namespaces.push_back(enumName);

		std::vector<luabridge::Namespace> nsStack;
		// begin + end for each namespace, and additional 1 for global namespace
		nsStack.reserve(namespaces.size() * 2 + 1);

		nsStack.push_back(luabridge::getGlobalNamespace(l));

		for (size_t i = 0; i < namespaces.size(); ++i) {
			nsStack.push_back(nsStack.back().beginNamespace(namespaces[i].data()));
		}
		
		for (auto entry : magic_enum::enum_entries<T>()) {
			nsStack.back().addVariable(entry.second.data(), entry.first);
		}

		for (size_t i = 0; i < namespaces.size(); ++i) {
			nsStack.push_back(nsStack.back().endNamespace());
		}

		while (nsStack.size()) {
			nsStack.pop_back();
		}
	}

	template<class T>
	void addClass(lua_State* l) {
		constexpr auto type = refl::reflect<T>();

		//spdlog::info(get_display_name(type));

		std::array<std::string, internal::MAX_NAMESPACES> namespaces;
		std::string className = internal::splitTypeName(get_display_name(type), namespaces);
		std::stack<luabridge::Namespace> nsStack;

		nsStack.push(luabridge::getGlobalNamespace(l));

		for (auto it = namespaces.begin(); it != namespaces.end(); ++it) {
			if (!it->empty()) {
				std::string name = std::string(*it);
				nsStack.push(nsStack.top().beginNamespace(name.c_str()));
			}
		}

		auto bridgeClass = nsStack.top().beginClass<T>(className.c_str());

		bridgeClass.addConstructor<void()>();

		if constexpr (std::is_base_of_v<fw::Object, T>) {
			bridgeClass.addConstructorFrom<std::shared_ptr<T>, void()>();
		}

		static constexpr auto members = filter(get_members(type), [&](auto member) {
			return fw::ReflUtil::isWritable<T>(member);
		});

		for_each(members, [&](auto member) {
			constexpr auto ptr = refl::descriptor::get_pointer(member);
			const std::string_view nameStr = get_display_name(member);

			if constexpr (refl::descriptor::is_property(member)) {
				if constexpr (!refl::descriptor::is_writable(member)) {
					/*if constexpr (refl::descriptor::has_attribute<setter_is_next>(member)) {
						using member_types = typename refl::type_descriptor<typename decltype(member)::declaring_type>::declared_member_types;
						constexpr auto member_index = refl::descriptor::detail::get_member_index(member);
						auto writer = refl::trait::get_t<member_index + 1, member_types>{};
						
						bridgeClass.addProperty(
									refl::descriptor::get_display_name(member),
									refl::descriptor::get_pointer(member),
									refl::descriptor::get_pointer(writer));
					} else {*/
						if constexpr (refl::descriptor::has_writer(member)) {
							//spdlog::info("\tProperty: {}", get_display_name(member));
							/*bridgeClass.addProperty(
										refl::descriptor::get_display_name(member),
										refl::descriptor::get_pointer(member),
										refl::descriptor::get_pointer(refl::descriptor::get_writer(member)));*/
						} else {
							//spdlog::info("\tRead Only Property: {}", get_display_name(member));
							//bridgeClass.addProperty(refl::descriptor::get_display_name(member), refl::descriptor::get_pointer(member));
						}
					//}
				}				
			} else if constexpr (refl::descriptor::is_function(member)) {
				//static_assert(ptr != nullptr);
				//spdlog::info("\tFunction: {}, {}", get_display_name(member), ptr != nullptr);
				if constexpr (ptr != nullptr) {
					bridgeClass.addFunction(get_display_name(member), ptr);
				}
			} else if constexpr (refl::descriptor::is_field(member)) {
				//static_assert(ptr != nullptr);
				spdlog::info("\tMember: {}, {}", get_display_name(member), ptr != nullptr);
				if constexpr (ptr != nullptr) {
					bridgeClass.addProperty(get_display_name(member), ptr);
				}
			}
		});

		auto bridgeNs = bridgeClass.endClass();
		for (size_t i = 1; i < nsStack.size(); ++i) {
			bridgeNs.endNamespace();
		}

		while (nsStack.size() > 1) {
			nsStack.pop();
		}
	}
}




/*
struct needs_classname : refl::attr::usage::type {};

template <typename Func>
struct ctor_ptr : refl::attr::usage::type
{
   using RT = std::remove_pointer_t<decltype(std::declval<Func>()(nullptr, nullptr))>;
   static_assert(std::is_same_v<Func, RT*(*)(void*, lua_State*)>, "Func must be a function pointer with signature RT*(*)(void*, lua_State*)");

   Func func;
   constexpr ctor_ptr(Func func) : func(func) {}
};

template <typename Func>
struct close_func_ptr : refl::attr::usage::type
{
   static_assert(std::is_member_function_pointer_v<std::remove_pointer_t<Func>> || std::is_function_v<std::remove_pointer_t<Func>>,
				 "Func must be a function pointer or method pointer");

   Func func;
   constexpr close_func_ptr(Func func) : func(func) {}
};

template <typename Func>
struct func_ptr : refl::attr::usage::function
{
   static_assert(std::is_member_function_pointer_v<std::remove_pointer_t<Func>> || std::is_function_v<std::remove_pointer_t<Func>>,
				 "Func must be a function pointer or method pointer");

   Func func;
   constexpr func_ptr(Func func) : func(func) {}
};

struct static_func : refl::attr::usage::function {};

struct setter_is_next : refl::attr::usage::function {};

#define USE_LUABRIDGE_HACK_FOR_REFLECTION 0

#if USE_LUABRIDGE_HACK_FOR_REFLECTION
// This code depends on the following changes to luabridge::Namespace::Class<T> that will never be merged back into the main fork:
//    1. make luabridge::Namespace::Class<T> public
//    2. make assertStackState public with 'using ClassBase::assertStackState'
//    3. add public state() method to expose the lua_State in luabridge::Namespace::Class<T>
// The main reason not to delete this code is that if we get into a situation where we need fast iteration of debug and build
// this code shaves about 20% off the build time compared to the fastest method that does not require the hack.
template<class T, class TS>
luabridge::Namespace::Class<T>& AddPropertySetter(luabridge::Namespace::Class<T> &bridgeClass, char const* name, void (T::*set)(TS))
{
   bridgeClass.assertStackState(); // Stack: const table (co), class table (cl), static table (st)

   typedef void (T::*set_t)(TS);
   new (lua_newuserdata(bridgeClass.state(), sizeof(set_t)))
   set_t(set); // Stack: co, cl, st, function ptr
   lua_pushcclosure(bridgeClass.state(), &luabridge::detail::CFunc::CallMember<set_t>::f, 1); // Stack: co, cl, st, setter
   luabridge::detail::CFunc::addSetter(bridgeClass.state(), name, -3); // Stack: co, cl, st

   return bridgeClass;
}
#endif

// use refl::util namespace to force errors if refl-cpp ever officially adds this
namespace refl
{
   namespace util
   {
	  template <typename F, typename... Ts>
	  constexpr void for_each_unordered(type_list<Ts...>, F&& f)
	  {
		  refl::util::ignore((f(Ts{}), 0)...);
	  }
   }
}

template<bool IS_STATIC, typename BC, typename Func, typename NC>
void AddMemberFunction(BC& bridgeClass, Func&& funcPtr, NC&& nameStr, _reflListMember& staticMethodList, _reflListMember& methodList)
{
   if constexpr (IS_STATIC)
   {
	  bridgeClass.addStaticFunction(nameStr.c_str(), funcPtr);
	  if (g_buildReflTables)
		 __AddToReflList(staticMethodList, nameStr.c_str());
   }
   else
   {
	  bridgeClass.addFunction(nameStr.c_str(), funcPtr);
	  if (g_buildReflTables)
		 __AddToReflList(methodList, nameStr.c_str());
   }
}

template<class T, typename TN>
void AddClassMembers(TN& bridgeClass, const char* className)
{
   constexpr auto type = refl::reflect<T>();
   constexpr auto members = get_members(type);

   _reflListMember &staticMethodList = g_staticMethods[className];
   _reflListMember &methodList = g_methods[className];
   _reflListMember &propGetList = g_propGets[className];
   _reflListMember &propSetList = g_propSets[className];
   _reflListMember &parentList = g_parents[className];

   if constexpr (refl::descriptor::has_attribute<needs_classname>(type))
   {
	  std::string nameStr(className);
	  AddMemberFunction<false>(
		   bridgeClass,
		   std::function<const char*(const T*)>([nameStr](const T*){ return nameStr.c_str(); } ),
		   std::string("ClassName"),
		   staticMethodList,
		   methodList);
   }

#if LUA_VERSION_NUM >= 504
   if constexpr (refl::descriptor::has_attribute<close_func_ptr>(type))
   {
	  // direct call to addFunction avoids adding "__close" to reflection list
	  bridgeClass.addFunction("__close", refl::descriptor::get_attribute<close_func_ptr>(type).func);
   }
#endif

   // WARNING: This for loop is quite difficult to refactor. If it works, I'd leave it alone.
   for_each_unordered(members, [&](auto member)
   {
	  constexpr bool isStaticFunc = refl::descriptor::has_attribute<static_func>(member);
	  if constexpr (refl::descriptor::has_attribute<special_name>(member))
	  {
		 if constexpr(refl::descriptor::has_attribute<func_ptr>(member))
			AddMemberFunction<isStaticFunc>(
				  bridgeClass,
				  refl::descriptor::get_attribute<func_ptr>(member).func,
				  REFL_MAKE_CONST_STRING(*refl::descriptor::get_attribute<special_name>(member).alternate_name),
				  staticMethodList,
				  methodList);
		 else
			AddMemberFunction<isStaticFunc>(
				  bridgeClass,
				  refl::descriptor::get_pointer(member),
				  REFL_MAKE_CONST_STRING(*refl::descriptor::get_attribute<special_name>(member).alternate_name),
				  staticMethodList,
				  methodList);
	  }
	  else
	  {
		 if constexpr(refl::descriptor::has_attribute<func_ptr>(member))
			AddMemberFunction<isStaticFunc>(
				  bridgeClass,
				  refl::descriptor::get_attribute<func_ptr>(member).func,
				  refl::descriptor::get_name(member),
				  staticMethodList,
				  methodList);
		 else
			AddMemberFunction<isStaticFunc>(
				  bridgeClass,
				  refl::descriptor::get_pointer(member),
				  refl::descriptor::get_name(member),
				  staticMethodList,
				  methodList);
	  }
	  if constexpr (refl::descriptor::is_property(member))
	  {
		 if constexpr (! refl::descriptor::is_writable(member))
		 {
			if constexpr (refl::descriptor::has_attribute<setter_is_next>(member))
			{
			   using member_types = typename refl::type_descriptor<typename decltype(member)::declaring_type>::declared_member_types;
			   constexpr auto member_index = refl::descriptor::detail::get_member_index(member);
			   auto writer = refl::trait::get_t<member_index + 1, member_types>{};
			   bridgeClass.addProperty(
						   refl::descriptor::get_display_name(member),
						   refl::descriptor::get_pointer(member),
						   refl::descriptor::get_pointer(writer));
			   if (g_buildReflTables)
				  __AddToReflList(propSetList, refl::descriptor::get_display_name(member));
			   if (g_buildReflTables)
				  __AddToReflList(propGetList, refl::descriptor::get_display_name(member));
			}
			else
			{
			   if constexpr (refl::descriptor::has_writer(member))
			   {
				  bridgeClass.addProperty(
							  refl::descriptor::get_display_name(member),
							  refl::descriptor::get_pointer(member),
							  refl::descriptor::get_pointer(refl::descriptor::get_writer(member)));
				  if (g_buildReflTables)
					 __AddToReflList(propSetList, refl::descriptor::get_display_name(member));

			   }
			   else
			   {
				  bridgeClass.addProperty(refl::descriptor::get_display_name(member), refl::descriptor::get_pointer(member));
			   }
			   if (g_buildReflTables)
				  __AddToReflList(propGetList, refl::descriptor::get_display_name(member));
			}
		 }
	  }
   });

   //add the properties every time
   bridgeClass.addStaticProperty(__METHOD_TABLE, &methodList , false);
   bridgeClass.addStaticProperty(__STATIC_METHOD_TABLE, &staticMethodList, false);
   bridgeClass.addStaticProperty(__PROPGET_TABLE, &propGetList, false);
   bridgeClass.addStaticProperty(__PROPSET_TABLE, &propSetList, false);
   bridgeClass.addStaticProperty(__PARENT_TABLE, &parentList, false);
}

template<class T>
static constexpr auto GetClassName_()
{
   constexpr auto type = refl::reflect<T>();
   if constexpr (refl::descriptor::has_attribute<special_name>(type))
	  return REFL_MAKE_CONST_STRING(*refl::descriptor::get_attribute<special_name>(type).alternate_name);
   else
	  return refl::descriptor::get_name(type);
}

using lbOptions = LB2(uint32_t) LB3(luabridge::Options);

template<class T>
void AddClass(const std::string& nameSpace, lua_State *l, LB2([[maybe_unused]])lbOptions options)
{
   constexpr auto name = GetClassName_<T>();

   auto bridgeClass = luabridge::getGlobalNamespace(l)
						.beginNamespace (nameSpace.c_str())
						   .beginClass<T>(name.c_str() LB3_PARM(options));

#if USE_PROXY_CONSTRUCTORS
   constexpr auto type = refl::reflect<T>();
   if constexpr (refl::descriptor::has_attribute<ctor_ptr>(type))
	  bridgeClass.addConstructor(refl::descriptor::get_attribute<ctor_ptr>(type).func);
#endif

   AddClassMembers<T>(bridgeClass, name.c_str());

   bridgeClass.endClass()
	  .endNamespace();
}

template<class T, typename ConstructorArgs>
void AddClass(const std::string& nameSpace, lua_State *l, LB2([[maybe_unused]])lbOptions options)
{
   constexpr auto name = GetClassName_<T>();

   auto bridgeClass = luabridge::getGlobalNamespace(l)
						.beginNamespace (nameSpace.c_str())
						   .beginClass<T>(name.c_str() LB3_PARM(options))
							  .template addConstructor<ConstructorArgs>();

   AddClassMembers<T>(bridgeClass, name.c_str());

   bridgeClass.endClass()
	  .endNamespace();
}

// for some reason, refl-cpp is generating compile-errors for its base_types attribute, so don't use it
// (the error occurs on addFunction, which may mean there is a conflict between refl-cpp and LuaBridge)
template<class T, class Baseclass>
void AddDerivedClass(const std::string& nameSpace, lua_State *l, LB2([[maybe_unused]])lbOptions options)
{
   constexpr auto name = GetClassName_<T>();

   auto bridgeClass = luabridge::getGlobalNamespace(l)
						.beginNamespace (nameSpace.c_str())
						.deriveClass<T, Baseclass>(name.c_str() LB3_PARM(options));

#if USE_PROXY_CONSTRUCTORS
   constexpr auto type = refl::reflect<T>();
   if constexpr (refl::descriptor::has_attribute<ctor_ptr>(type))
	  bridgeClass.addConstructor(refl::descriptor::get_attribute<ctor_ptr>(type).func);
#endif

   AddClassMembers<T>(bridgeClass, name.c_str());

   bridgeClass.endClass()
	  .endNamespace();

   if (g_buildReflTables)
	  __AddToReflList(g_parents[name.c_str()], GetClassName_<Baseclass>().c_str(), "table");
}

// for some reason, refl-cpp is generating compile-errors for its base_types attribute, so don't use it
// (the error occurs on addFunction, which may mean there is a conflict between refl-cpp and LuaBridge)
template<class T, class Baseclass, typename ConstructorArgs>
void AddDerivedClass(const std::string& nameSpace, lua_State *l, LB2([[maybe_unused]])lbOptions options)
{
   constexpr auto name = GetClassName_<T>();

   auto bridgeClass = luabridge::getGlobalNamespace(l)
						.beginNamespace (nameSpace.c_str())
						   .deriveClass<T, Baseclass>(name.c_str() LB3_PARM(options))
							  .template addConstructor<ConstructorArgs>();

   AddClassMembers<T>(bridgeClass, name.c_str());

   bridgeClass.endClass()
	  .endNamespace();

   if (g_buildReflTables)
	  __AddToReflList(g_parents[name.c_str()], GetClassName_<Baseclass>().c_str(), "table");
}
*/