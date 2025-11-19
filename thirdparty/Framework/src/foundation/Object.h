#pragma once

#include <memory>
#include <string_view>

#include <entt/core/type_info.hpp>

#include "foundation/Types.h"

namespace fw {
	// Helper template to check inheritance at compile time
	template<typename... Bases>
	struct BaseChecker {
		static bool checkType(entt::type_info) { return false; }
	};

	template<typename First, typename... Rest>
	struct BaseChecker<First, Rest...> {
		static bool checkType(entt::type_info typeInfo) {
			return typeInfo == entt::type_id<First>() ||
				First::isDerivedFromTypeInfo_static(typeInfo) ||
				BaseChecker<Rest...>::checkType(typeInfo);
		}
	};

	class Object : public std::enable_shared_from_this<Object> {
	public:
		virtual entt::type_info getTypeInfo() const = 0;

		virtual uint32 getTypeId() const = 0;

		virtual std::string_view getTypeName() const = 0;

		// Virtual function for runtime polymorphic checking
		virtual bool isDerivedFromTypeInfo(entt::type_info typeInfo) const {
			return typeInfo == entt::type_id<Object>();
		}

		// Static helper for compile-time chain
		static bool isDerivedFromTypeInfo_static(entt::type_info typeInfo) {
			return typeInfo == entt::type_id<Object>();
		}

		template <typename T = Object>
		std::shared_ptr<T> sharedFromThis() {
			return std::static_pointer_cast<T>(shared_from_this());
		}

		template <typename T = Object>
		std::shared_ptr<const T> sharedFromThis() const {
			return std::static_pointer_cast<const T>(shared_from_this());
		}

		template <typename T>
		bool isType() const {
			return getTypeInfo() == entt::type_id<T>();
		}

		template <typename T>
		bool isDerivedFrom() const {
			return isDerivedFromTypeInfo(entt::type_id<T>());
		}

		template <typename T>
		T* asRaw() {
			return static_cast<T*>(this);
		}
	};
}

#define FwRegisterObject(...) \
	public: \
	virtual entt::type_info getTypeInfo() const override { return entt::type_id<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>(); } \
	virtual uint32 getTypeId() const override { return entt::type_hash<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>::value(); } \
	virtual std::string_view getTypeName() const override { return entt::type_name<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>::value(); } \
	virtual bool isDerivedFromTypeInfo(entt::type_info typeInfo) const override { return getTypeInfo() == typeInfo || fw::BaseChecker<__VA_ARGS__>::checkType(typeInfo); } \
	private:
