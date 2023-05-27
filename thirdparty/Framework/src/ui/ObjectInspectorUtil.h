#pragma once

#include <refl.hpp>

#include "foundation/Attributes.h"
#include "ObjectInspectorView.h"

namespace fw::ObjectInspectorUtil {
	template <typename T>
	void reflect(PropertyEditorViewPtr inspector, T& item) {
		static constexpr auto members = filter(refl::member_list<T>{}, [](auto member) { return is_readable(member) && has_writer(member); });
		
		for_each(members, [&](auto member) {
			auto reader = get_reader(member);
			auto writer = get_writer(member);
			using MemberType = std::decay_t<decltype(reader(item))>;
			
			if constexpr (std::is_same_v<MemberType, bool>) {
				DropDownMenuViewPtr dropdown = inspector->addProperty<DropDownMenuView>(get_display_name(member));

				dropdown->setItems(std::vector<std::string>{ "false", "true" });
				dropdown->setValue(reader(item) == false ? 0 : 1);
				dropdown->ValueChangeEvent = [&](int32 v) {
					writer(item, v != 0);
				};
			} else if constexpr (std::is_floating_point_v<MemberType> || std::is_integral_v<MemberType>) {
				SliderViewPtr slider = inspector->addProperty<SliderView>(get_display_name(member));

				if constexpr (refl::descriptor::has_attribute<RangeAttribute>(member)) {
					auto&& attrib = refl::descriptor::get_attribute<RangeAttribute>(member);
					slider->setRange(attrib.min, attrib.max);
				}

				if constexpr (refl::descriptor::has_attribute<StepSizeAttribute>(member)) {
					auto&& attrib = refl::descriptor::get_attribute<StepSizeAttribute>(member);
					slider->setStepSize(attrib.value);
				} else if constexpr (std::is_integral_v<MemberType>) {
					slider->setStepSize(1.0f);
				}

				slider->setValue(static_cast<f32>(reader(item)));
				slider->ValueChangeEvent = [&](f32 v) {
					writer(item, static_cast<MemberType>(v));
				};
			} else if constexpr (std::is_enum_v<MemberType>) {
				DropDownMenuViewPtr dropdown = inspector->addProperty<DropDownMenuView>(get_display_name(member));

				constexpr auto names = magic_enum::enum_names<MemberType>();
				std::vector<std::string> items;
				for (const auto& entry : names) { items.push_back(std::string(entry)); }

				dropdown->setItems(items);

				const auto idx = magic_enum::enum_index<MemberType>(reader(item));
				if (idx.has_value()) {
					dropdown->setValue(static_cast<int32>(idx.value()));
				}

				dropdown->ValueChangeEvent = [&](int32 v) {
					if (v >= 0 && v < magic_enum::enum_count<MemberType>()) {
						writer(item, magic_enum::enum_value<MemberType>(v));
					}
				};
			}
		});
	}

	template <typename T>
	void reflectAny(ObjectInspectorViewPtr inspector, entt::any& item) {
		reflect<T>(inspector, entt::any_cast<T>(item));
	}
}
