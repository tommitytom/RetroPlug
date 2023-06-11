#pragma once

#include <refl.hpp>

#include "foundation/Attributes.h"
#include "ObjectInspectorView.h"
#include "ui/Flex.h"
#include "ui/TextEditView.h"

namespace fw::ObjectInspectorUtil {
	namespace internal {
		template <typename ClassType, typename MemberType, typename MemberDescriptor>
		void propSetter(ClassType& item, const std::decay_t<MemberType>& value, MemberDescriptor member) {
			using UnderlyingMemberType = std::decay_t<MemberType>;
			
			
		}
	}

	template <typename T>
	void reflect(PropertyEditorViewPtr inspector, T& item) {
		static constexpr auto members = filter(refl::member_list<T>{}, [&](auto member) {
			if constexpr (is_readable(member)) {
				return has_writer(member);
			}

			if constexpr (!has_reader(member) && !has_writer(member)) {
				using Descriptor = decltype(member);
				using MemberType = typename Descriptor::template return_type<typename Descriptor::declaring_type&>;
				if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
					return true;
				}
			}
		
			return false;
		});
		
		for_each(members, [&](auto member) {
			using Descriptor = decltype(member);
			using MemberType = typename Descriptor::template return_type<typename Descriptor::declaring_type&>;
			using UnderlyingMemberType = std::decay_t<MemberType>;

			if constexpr (std::is_same_v<UnderlyingMemberType, bool>) {
				DropDownMenuViewPtr dropdown = inspector->addProperty<DropDownMenuView>(get_display_name(member));

				dropdown->setItems(std::vector<std::string>{ "false", "true" });
				
				if constexpr (has_reader(member)) {
					auto reader = get_reader(member);
					dropdown->setValue(reader(item) == false ? 0 : 1);
				} else {
					dropdown->setValue(member(item) == false ? 0 : 1);
				}				

				dropdown->ValueChangeEvent = [&](int32 v) { propSetter(member, item, v != 0); };
			} else if constexpr (std::is_floating_point_v<UnderlyingMemberType> || std::is_integral_v<UnderlyingMemberType>) {
				SliderViewPtr slider = inspector->addProperty<SliderView>(get_display_name(member));

				if constexpr (refl::descriptor::has_attribute<RangeAttribute>(member)) {
					auto&& attrib = refl::descriptor::get_attribute<RangeAttribute>(member);
					slider->setRange(attrib.min, attrib.max);
				}

				if constexpr (refl::descriptor::has_attribute<StepSizeAttribute>(member)) {
					auto&& attrib = refl::descriptor::get_attribute<StepSizeAttribute>(member);
					slider->setStepSize(attrib.value);
				} else if constexpr (std::is_integral_v<UnderlyingMemberType>) {
					slider->setStepSize(1.0f);
				}

				if constexpr (has_reader(member)) {
					auto reader = get_reader(member);
					slider->setValue(static_cast<f32>(reader(item)));
				} else {
					slider->setValue(static_cast<f32>(member(item)));
				}

				slider->ValueChangeEvent = [&](f32 v) {
					if constexpr (has_writer(member)) {
						auto writer = get_writer(member);
						writer(item, static_cast<UnderlyingMemberType>(v));
					} else {
						if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
							member(item) = static_cast<UnderlyingMemberType>(v);
						} else {
							//spdlog::info("Failed to set slider value: {}", v);
						}
					}
				};
			} else if constexpr (std::is_enum_v<UnderlyingMemberType>) {
				DropDownMenuViewPtr dropdown = inspector->addProperty<DropDownMenuView>(get_display_name(member));

				constexpr auto names = magic_enum::enum_names<UnderlyingMemberType>();
				std::vector<std::string> items;
				for (const auto& entry : names) { items.push_back(std::string(entry)); }

				dropdown->setItems(items);

				if constexpr (has_reader(member)) {
					auto reader = get_reader(member);
					const auto idx = magic_enum::enum_index<UnderlyingMemberType>(reader(item));
					if (idx.has_value()) {
						dropdown->setValue(static_cast<int32>(idx.value()));
					}
				} else {
					const auto idx = magic_enum::enum_index<UnderlyingMemberType>(member(item));
					if (idx.has_value()) {
						dropdown->setValue(static_cast<int32>(idx.value()));
					}
				}

				dropdown->ValueChangeEvent = [&](int32 v) {
					if (v >= 0 && v < magic_enum::enum_count<UnderlyingMemberType>()) {
						if constexpr (has_writer(member)) {
							auto writer = get_writer(member);
							writer(item, static_cast<UnderlyingMemberType>(v));
						} else {
							if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
								member(item) = static_cast<UnderlyingMemberType>(v);
							} else {
								//spdlog::info("Failed to set slider value: {}", v);
							}
						}
						//propSetter(member, item, magic_enum::enum_value<UnderlyingMemberType>(v));
					}
				};
			} else if constexpr (std::is_same_v<UnderlyingMemberType, std::string>) {
				TextEditViewPtr textEdit = inspector->addProperty<TextEditView>(get_display_name(member));

				if constexpr (has_reader(member)) {
					auto reader = get_reader(member);
					textEdit->setText(reader(item));
				} else {
					textEdit->setText(member(item));
				}

				textEdit->TextChangeEvent = [&](const std::string& v) {
					if constexpr (has_writer(member)) {
						auto writer = get_writer(member);
						writer(item, static_cast<UnderlyingMemberType>(v));
					} else {
						if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
							member(item) = static_cast<UnderlyingMemberType>(v);
						}
					}
				};
			} else if constexpr (std::is_same_v<UnderlyingMemberType, FlexValue>) {
				FlexValueEditViewPtr flexValueEdit = inspector->addProperty<FlexValueEditView>(get_display_name(member));

				if constexpr (has_reader(member)) {
					auto reader = get_reader(member);
					flexValueEdit->setValue(reader(item));
				} else {
					flexValueEdit->setValue(member(item));
				}

				flexValueEdit->ValueChangeEvent = [&](const FlexValue &v) {
					if constexpr (has_writer(member)) {
						auto writer = get_writer(member);
						writer(item, static_cast<UnderlyingMemberType>(v));
					} else {
						if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
							member(item) = static_cast<UnderlyingMemberType>(v);
						}
					}
				};
			} else if constexpr (std::is_class_v<UnderlyingMemberType>) {
				if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
					reflect(inspector, member(item));
				} else {
					spdlog::info("Failed to reflect class: Member {} is not accessible", get_display_name(member));
				}				
			}
		});
	}

	template <typename T>
	void reflectAny(ObjectInspectorViewPtr inspector, entt::any& item) {
		reflect<T>(inspector, entt::any_cast<T>(item));
	}
}
