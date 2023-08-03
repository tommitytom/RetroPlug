#pragma once

#include <memory>
#include <string_view>

#include <entt/core/type_info.hpp>

#include "foundation/Types.h"

namespace fw {
	class Object : public std::enable_shared_from_this<Object> {
	public:
		virtual uint32 getTypeId() const = 0;

		virtual std::string_view getTypeName() const = 0;

		template <typename T = Object>
		std::shared_ptr<T> sharedFromThis() {
			return std::static_pointer_cast<T>(shared_from_this());
		}

		template <typename T = Object>
		std::shared_ptr<const T> sharedFromThis() const {
			return std::static_pointer_cast<const T>(shared_from_this());
		}
	};
}

#define RegisterObject() \
	public: \
	virtual uint32 getTypeId() const override { return entt::type_hash<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>::value(); } \
	virtual std::string_view getTypeName() const override { return entt::type_name<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>::value(); } \
	private: