#pragma once

#include <refl.hpp>
#include "ui/View.h"
#include <entt/entt.hpp>

namespace fw {
	using PropertySetter = std::function<void(entt::any&&)>;
	using PropertyGetter = std::function<entt::any()>;
	
	struct PropertyGridItem {
		std::string_view name;
		entt::any value;
		PropertyGetter getter;
		PropertySetter setter;
	};

	class PropertyGrid {
	private:
		std::vector<PropertyGridItem> _properties;

	public:
		void addProperty(std::string_view name, entt::any&& value, PropertyGetter getter, PropertySetter setter) {
			assert(!value.owner());
			
			_properties.emplace_back(PropertyGridItem{
				.name = name,
				.value = std::move(value),
				.getter = std::move(getter),
				.setter = std::move(setter)
			});
		}
	};
}

namespace fw::PropertyInspector {
	template <typename T>
	void makeSetter(entt::any& prop, entt::any&& value) {
		auto p = entt::any_cast<T&>(prop);
		auto v = entt::any_cast<T&>(value);
		
		//value = 
	}

	template <typename T>
	void inspectObject(ViewPtr target) {
		PropertyGrid grid;

		for_each(refl::reflect(target).members, [&](auto member) {
			using MemberType = std::decay_t<decltype(member(target))>;

			if constexpr (is_readable(member)) {
				//grid.addProperty(get_display_name(member))
			}
		});
	}
}