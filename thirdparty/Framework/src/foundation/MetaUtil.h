#pragma once

#include <string_view>
#include <entt/meta/factory.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <entt/core/hashed_string.hpp>

#include "foundation/StringUtil.h"

using namespace entt::literals;

namespace fw::MetaUtil {
	inline std::string_view getTypeName(entt::id_type typeId) {
		entt::meta_type type = entt::resolve(typeId);

		std::string_view typeName = type.info().name();
		if (typeName.starts_with("class ")) {
			return typeName.substr(6);
		}

		if (typeName.starts_with("struct ")) {
			return typeName.substr(7);
		}

		return typeName;
	}

	inline std::string_view getName(const entt::meta_type& type) {
		return type.prop("Name"_hs).value().cast<std::string_view>();
	}

	inline std::string_view getName(const entt::meta_data& data) {
		return data.prop("Name"_hs).value().cast<std::string_view>();
	}

	inline std::vector<entt::meta_data> getSortedFields(entt::meta_type type) {
		std::vector<entt::meta_data> fields;
		for (entt::meta_data field : type.data()) {
			fields.push_back(field);
		}

		std::sort(fields.begin(), fields.end(), [](const entt::meta_data& a, const entt::meta_data& b) -> bool {
			return a.prop("Order"_hs).value().cast<size_t>() < b.prop("Order"_hs).value().cast<size_t>();
		});

		return fields;
	}

	template <typename T>
	inline void registerType(std::string_view name) {
		entt::meta<T>()
			.type(entt::hashed_string{ name.data() })
			.prop("Name"_hs, name)
			.ctor();
	}

	template <typename T>
	inline std::vector<std::string> getEnumNames(bool formatted) {
		std::vector<entt::meta_data> fields = getSortedFields(entt::resolve<T>());
		std::vector<std::string> ret;		

		for (entt::meta_data data : fields) {
			std::string name(getName(data));

			if (formatted) {
				name = fw::StringUtil::formatMemberName(name);
			}

			ret.push_back(std::move(name));
		}

		return ret;
	}

	template <typename T>
	bool tryGetProp(const entt::meta_data& field, T& target) {
		entt::meta_prop prop = field.prop(entt::type_id<T>().hash());
		if (prop) {
			T* value = prop.value().try_cast<T>();
			target = *value;
			return true;
		}

		return false;
	}
}
