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
			const fw::Field& field;
			entt::any value;
			PropertyEditorBasePtr editor;
		};

		struct FieldGroup {
			std::string name;
			std::vector<FieldWrapper> fields;
		};

		std::vector<FieldGroup> _fieldGroups;

	public:
		template <typename T>
		std::shared_ptr<T> findEditor(const fw::Field& field) {
			for (const FieldGroup& group : _fieldGroups) {
				for (const FieldWrapper& wrapper : group.fields) {
					if (wrapper.field.type == field.type) {
						return std::static_pointer_cast<T>(wrapper.editor);
					}
				}
			}

			return nullptr;
		}

		void addObject(const fw::TypeRegistry& reg, std::string_view name, fw::TypeInstance objInstance) {
			size_t fieldId = 0;
			size_t groupId = _fieldGroups.size();
			
			entt::any& obj = objInstance.getValue();
			assert(!obj.owner());

			const fw::TypeInfo* objType = reg.findTypeInfo(obj);
			assert(objType);

			Group& group = pushGroup(name);
			FieldGroup fieldGroup;

			for (const fw::Field& field : objType->fields) {
				FieldWrapper fieldWrap = {
					.field = field,
					.value = field.get(obj)
				};

				const fw::TypeInfo& fieldType = reg.getTypeInfo(field.type);
				
				if (fieldType.isEnum()) {
					fieldWrap.editor = createDropDown(reg, field.name, field, fieldWrap.value, groupId, fieldId);
				} else if (fieldType.isType<bool>()) {
					//fieldWrap.editor = createCheckbox(field.name, field, fieldWrap.value, groupId, fieldId);
				} else if (fieldType.isIntegral() || fieldType.isFloat()) {
					fieldWrap.editor = createSlider(field.name, field, fieldWrap.value, groupId, fieldId);
				} else if (fieldType.isClass()) {
					if (fieldType.isType<std::string>()) {
						if (const TypedProperty<UriBrowser>* uriBrowser = fieldType.findProperty<UriBrowser>(); uriBrowser) {
							std::vector<std::string> uris = getResourceManager().getUris(uriBrowser->getValue().getItems());

							DropDownMenuViewPtr dropdown = addProperty<DropDownMenuView>(name);

							dropdown->setItems(uris);
							//dropdown->setValue((int32)value);

							dropdown->ValueChangeEvent = [groupId, fieldId, this](int32 v) {
								bool valid = _fieldGroups[groupId].fields[fieldId].value.assign(v);
								assert(valid);
							};
						}
					}
				}

				fieldGroup.fields.push_back(std::move(fieldWrap));
				fieldId++;
			}

			_fieldGroups.push_back(std::move(fieldGroup));
		}

		PropertyEditorBasePtr getPropertyEditor(const fw::Field& field) {
			for (const FieldGroup& group : _fieldGroups) {
				for (const FieldWrapper& wrapper : group.fields) {
					if (wrapper.field == field) {
						return wrapper.editor;
					}
				}
			}

			return nullptr;
		}

		template <typename T>
		T anyToNumber(entt::any& value) {
			static_assert(std::is_arithmetic_v<T>);

			fw::TypeId typeId = fw::getTypeId(value);

			if (typeId == fw::getTypeId<f32>()) {
				return static_cast<T>(entt::any_cast<f32>(value));
			} else if (typeId == fw::getTypeId<f64>()) {
				return static_cast<T>(entt::any_cast<f64>(value));
			} else if (typeId == fw::getTypeId<int8>()) {
				return static_cast<T>(entt::any_cast<int8>(value));
			} else if (typeId == fw::getTypeId<int16>()) {
				return static_cast<T>(entt::any_cast<int16>(value));
			} else if (typeId == fw::getTypeId<int32>()) {
				return static_cast<T>(entt::any_cast<int32>(value));
			} else if (typeId == fw::getTypeId<int64>()) {
				return static_cast<T>(entt::any_cast<int64>(value));
			} else if (typeId == fw::getTypeId<uint8>()) {
				return static_cast<T>(entt::any_cast<uint8>(value));
			} else if (typeId == fw::getTypeId<uint16>()) {
				return static_cast<T>(entt::any_cast<uint16>(value));
			} else if (typeId == fw::getTypeId<uint32>()) {
				return static_cast<T>(entt::any_cast<uint32>(value));
			} else if (typeId == fw::getTypeId<uint64>()) {
				return static_cast<T>(entt::any_cast<uint64>(value));
			} else if (typeId == fw::getTypeId<bool>()) {
				return static_cast<T>(entt::any_cast<bool>(value));
			}
			
			assert(false);

			return 0;
		}

		template <typename T>
		entt::any numberToAny(T num, TypeId targetType) {
			static_assert(std::is_arithmetic_v<T>);

			if (targetType == fw::getTypeId<f32>()) {
				return entt::any(static_cast<f32>(num));
			} else if (targetType == fw::getTypeId<f64>()) {
				return entt::any(static_cast<f64>(num));
			} else if (targetType == fw::getTypeId<int8>()) {
				return entt::any(static_cast<int8>(num));
			} else if (targetType == fw::getTypeId<int16>()) {
				return entt::any(static_cast<int16>(num));
			} else if (targetType == fw::getTypeId<int32>()) {
				return entt::any(static_cast<int32>(num));
			} else if (targetType == fw::getTypeId<int64>()) {
				return entt::any(static_cast<int64>(num));
			} else if (targetType == fw::getTypeId<uint8>()) {
				return entt::any(static_cast<uint8>(num));
			} else if (targetType == fw::getTypeId<uint16>()) {
				return entt::any(static_cast<uint16>(num));
			} else if (targetType == fw::getTypeId<uint32>()) {
				return entt::any(static_cast<uint32>(num));
			} else if (targetType == fw::getTypeId<uint64>()) {
				return entt::any(static_cast<uint64>(num));
			} else if (targetType == fw::getTypeId<bool>()) {
				return entt::any(static_cast<bool>(num));
			}

			assert(false);

			return 0;
		}

		SliderViewPtr createSlider(std::string_view nameView, const fw::Field& field, entt::any& value, size_t groupId, size_t fieldId) {
			assert(!value.owner());

			std::string name;
			if (const TypedProperty<DisplayName>* displayName = field.findProperty<DisplayName>(); displayName) {
				name = displayName->getValue().getName();
			} else {
				name = StringUtil::formatMemberName(nameView);
			}

			SliderViewPtr slider = addProperty<SliderView>(name);

			if (const TypedProperty<Range>* range = field.findProperty<Range>(); range) {
				slider->setRange(range->getValue().getMin(), range->getValue().getMax());
			}

			if (const TypedProperty<StepSize>* stepSize = field.findProperty<StepSize>(); stepSize) {
				slider->setStepSize(stepSize->getValue().getValue());
			}

			/*if (Curve curve; MetaUtil::tryGetProp<Curve>(field, curve)) {
				slider->setCurve(curve.getFunc());
			}*/

			slider->setValue(anyToNumber<f32>(value));

			slider->ValueChangeEvent = [groupId, fieldId, this](f32 v) {
				auto& field = _fieldGroups[groupId].fields[fieldId];
				assert(!field.value.owner());

				bool valid = field.value.assign(numberToAny(v, field.field.type));
				assert(valid);

				assert(!field.value.owner());
			};

			return slider;
		}

		DropDownMenuViewPtr createDropDown(const TypeRegistry& reg, std::string_view nameView, const fw::Field& field, entt::any& value, size_t groupId, size_t fieldId) {
			assert(reg.getTypeInfo(field.type).isEnum());

			std::vector<std::string> items;

			const fw::TypeInfo& enumType = reg.getTypeInfo(field.type);

			for (const fw::Field& enumField : enumType.fields) {
				std::string enumFieldName = StringUtil::formatMemberName(enumField.name);
				items.push_back(enumFieldName);
			}

			std::string name;
			if (const TypedProperty<DisplayName>* displayName = field.findProperty<DisplayName>(); displayName) {
				name = displayName->getValue().getName();
			} else {
				name = StringUtil::formatMemberName(nameView);
			}

			DropDownMenuViewPtr dropdown = addProperty<DropDownMenuView>(name);

			dropdown->setItems(items);
			dropdown->setValue(anyToNumber<int32>(value));

			dropdown->ValueChangeEvent = [groupId, fieldId, this](int32 v) {
				assert(!_fieldGroups[groupId].fields[fieldId].value.owner());

				entt::any val = _fieldGroups[groupId].fields[fieldId].value.as_ref();
				assert(!val.owner());

				bool valid = val.assign(numberToAny(v, fw::getTypeId(val)));
				assert(valid);
				assert(!val.owner());

				assert(!_fieldGroups[groupId].fields[fieldId].value.owner());
			};

			return dropdown;
		}
	};

	using ObjectInspectorViewPtr = std::shared_ptr<ObjectInspectorView>;
}
