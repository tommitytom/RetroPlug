#pragma once

#include <entt/meta/fwd.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>

#include "foundation/Curves.h"
#include "foundation/MetaProperties.h"
#include "foundation/MetaUtil.h"

#include "ui/DropDownMenuView.h"
#include "ui/PropertyEditorView.h"
#include "ui/SliderView.h"

namespace fw {
	class ObjectInspectorView : public PropertyEditorView {
	private:
		struct FieldWrapper {
			entt::meta_data field;
			entt::meta_any value;
			PropertyEditorBasePtr editor;
		};

		struct FieldGroup {
			std::string name;
			std::vector<FieldWrapper> fields;
		};

		std::vector<FieldGroup> _fieldGroups;

	public:
		template <typename T>
		std::shared_ptr<T> findEditor(entt::meta_data field) {
			for (const FieldGroup& group : _fieldGroups) {
				for (const FieldWrapper& wrapper : group.fields) {
					if (wrapper.field.id() == field.id()) {
						return std::static_pointer_cast<T>(wrapper.editor);
					}
				}
			}

			return nullptr;
		}

		void addObject(std::string_view name, entt::meta_any obj) {
			size_t fieldId = 0;
			size_t groupId = _fieldGroups.size();

			entt::meta_type objType = obj.type();
			assert(objType.id() != 0);

			Group& group = pushGroup(name);
			FieldGroup fieldGroup;

			std::vector<entt::meta_data> fields = MetaUtil::getSortedFields(objType);

			for (const entt::meta_data& field : fields) {
				FieldWrapper fieldWrap = {
					.field = field,
					.value = field.get(obj)
				};

				entt::meta_type fieldType = field.type();
				entt::meta_prop nameProp = field.prop("Name"_hs);
				std::string_view name = nameProp.value().cast<std::string_view>();

				if (fieldType.is_enum()) {
					entt::meta_any v = fieldWrap.value;
					v.allow_cast<int32>();
					int32 value = v.cast<int32>();
					fieldWrap.editor = createDropDown<int32>(name, field, value, groupId, fieldId);
				} else if (fieldType.is_arithmetic()) {
					if (fieldType == entt::resolve<f32>()) {
						fieldWrap.editor = createSlider<f32>(name, field, fieldWrap.value.cast<f32>(), groupId, fieldId);
					} else if (fieldType == entt::resolve<size_t>()) {
						fieldWrap.editor = createSlider<size_t>(name, field, fieldWrap.value.cast<size_t>(), groupId, fieldId);
					}
				} else if (fieldType.is_class()) {
					if (fieldType == entt::resolve<std::string>()) {
						if (UriBrowser uriBrowser; MetaUtil::tryGetProp<UriBrowser>(field, uriBrowser)) {
							std::vector<std::string> uris = getResourceManager().getUris(uriBrowser.getItems());

							DropDownMenuViewPtr dropdown = addProperty<DropDownMenuView>(name);

							dropdown->setItems(uris);
							//dropdown->setValue((int32)value);

							dropdown->ValueChangeEvent = [groupId, fieldId, this](int32 v) {
								_fieldGroups[groupId].fields[fieldId].value.assign(v);
							};
						}
					}
				}

				fieldGroup.fields.push_back(std::move(fieldWrap));
				fieldId++;
			}

			_fieldGroups.push_back(std::move(fieldGroup));
		}

		template <typename T>
		void addObject(std::string_view name, T& obj) {
			addObject(name, entt::meta_handle(obj)->as_ref());
		}

		PropertyEditorBasePtr getPropertyEditor(entt::meta_data field) {
			for (const FieldGroup& group : _fieldGroups) {
				for (const FieldWrapper& wrapper : group.fields) {
					if (wrapper.field.id() == field.id()) {
						return wrapper.editor;
					}
				}
			}

			return nullptr;
		}

		template <typename T>
		SliderViewPtr createSlider(std::string_view nameView, entt::meta_data field, T value, size_t groupId, size_t fieldId) {
			std::string name;
			if (DisplayName displayName; MetaUtil::tryGetProp<DisplayName>(field, displayName)) {
				name = displayName.getName();
			} else {
				name = StringUtil::formatMemberName(nameView);
			}

			SliderViewPtr slider = addProperty<SliderView>(name);

			if (Range range; MetaUtil::tryGetProp<Range>(field, range)) {
				slider->setRange(range.getMin(), range.getMax());
			}

			if (StepSize stepSize; MetaUtil::tryGetProp<StepSize>(field, stepSize)) {
				slider->setStepSize(stepSize.getValue());
			}

			/*if (Curve curve; MetaUtil::tryGetProp<Curve>(field, curve)) {
				slider->setCurve(curve.getFunc());
			}*/

			slider->setValue((f32)value);

			slider->ValueChangeEvent = [groupId, fieldId, this](f32 v) {
				_fieldGroups[groupId].fields[fieldId].value.assign((T)v);
			};

			return slider;
		}

		template <typename T>
		DropDownMenuViewPtr createDropDown(std::string_view nameView, entt::meta_data field, T value, size_t groupId, size_t fieldId) {
			assert(field.type().is_enum());

			std::vector<std::string> items;

			std::vector<entt::meta_data> fields = MetaUtil::getSortedFields(field.type());

			for (entt::meta_data enumField : fields) {
				std::string enumFieldName = StringUtil::formatMemberName(MetaUtil::getName(enumField));
				items.push_back(enumFieldName);
			}

			std::string name;
			if (DisplayName displayName; MetaUtil::tryGetProp<DisplayName>(field, displayName)) {
				name = displayName.getName();
			} else {
				name = StringUtil::formatMemberName(nameView);
			}

			DropDownMenuViewPtr dropdown = addProperty<DropDownMenuView>(name);

			dropdown->setItems(items);
			dropdown->setValue((int32)value);

			dropdown->ValueChangeEvent = [groupId, fieldId, this](int32 v) {
				_fieldGroups[groupId].fields[fieldId].value.assign((T)v);
			};

			return dropdown;
		}
	};

	using ObjectInspectorViewPtr = std::shared_ptr<ObjectInspectorView>;
}
