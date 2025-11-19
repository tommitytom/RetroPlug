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

namespace orb {
	class ObjectInspectorView : public PropertyEditorView {
		FwRegisterObject();
	private:
		struct FieldWrapper {
			const orb::Field& field;
			entt::any value;
			PropertyEditorBasePtr editor;
		};

		struct FieldGroup {
			std::string name;
			std::vector<FieldWrapper> fields;
		};

		std::vector<FieldGroup> _fieldGroups;

	public:
		void clear() {
			_fieldGroups.clear();
			clearProperties();
			removeChildren();
		}
		
		template <typename T>
		std::shared_ptr<T> findEditor(const orb::Field& field) {
			for (const FieldGroup& group : _fieldGroups) {
				for (const FieldWrapper& wrapper : group.fields) {
					if (wrapper.field.type == field.type) {
						return std::static_pointer_cast<T>(wrapper.editor);
					}
				}
			}

			return nullptr;
		}

		void addView(const orb::TypeRegistry& reg, ViewPtr view) {
			addObject(reg, view->getName() + " Layout", view->getLayout());
		}

		void addObject(const orb::TypeRegistry& reg, std::string_view name, orb::TypeInstance objInstance) {
			size_t fieldId = 0;
			size_t groupId = _fieldGroups.size();
			
			entt::any& obj = objInstance.getValue();
			assert(!obj.owner());

			const orb::TypeInfo* objType = reg.findTypeInfo(obj);
			assert(objType);

			//Group& group = pushGroup(name);
			FieldGroup fieldGroup;

			for (const orb::Field& field : objType->fields) {
				FieldWrapper fieldWrap = {
					.field = field,
					.value = field.get(obj)
				};

				const orb::TypeInfo& fieldType = reg.getTypeInfo(field.type);
				
				if (fieldType.isEnum()) {
					fieldWrap.editor = createDropDown(reg, field.name, field, fieldWrap.value, groupId, fieldId);
				} else if (fieldType.isType<bool>()) {
					//fieldWrap.editor = createCheckbox(field.name, field, fieldWrap.value, groupId, fieldId);
				} else if (fieldType.isIntegral() || fieldType.isFloat()) {
					fieldWrap.editor = createSlider(field.name, field, fieldWrap.value, groupId, fieldId);
				} else if (fieldType.isClass()) {
					if (fieldType.isType<std::string>()) {
						if (const TypedAttribute<UriBrowser>* uriBrowser = fieldType.findAttribute<UriBrowser>(); uriBrowser) {
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

		PropertyEditorBasePtr getPropertyEditor(const orb::Field& field) {
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

			orb::TypeId typeId = orb::getTypeId(value);

			if (typeId == orb::getTypeId<f32>()) {
				return static_cast<T>(entt::any_cast<f32>(value));
			} else if (typeId == orb::getTypeId<f64>()) {
				return static_cast<T>(entt::any_cast<f64>(value));
			} else if (typeId == orb::getTypeId<int8>()) {
				return static_cast<T>(entt::any_cast<int8>(value));
			} else if (typeId == orb::getTypeId<int16>()) {
				return static_cast<T>(entt::any_cast<int16>(value));
			} else if (typeId == orb::getTypeId<int32>()) {
				return static_cast<T>(entt::any_cast<int32>(value));
			} else if (typeId == orb::getTypeId<int64>()) {
				return static_cast<T>(entt::any_cast<int64>(value));
			} else if (typeId == orb::getTypeId<uint8>()) {
				return static_cast<T>(entt::any_cast<uint8>(value));
			} else if (typeId == orb::getTypeId<uint16>()) {
				return static_cast<T>(entt::any_cast<uint16>(value));
			} else if (typeId == orb::getTypeId<uint32>()) {
				return static_cast<T>(entt::any_cast<uint32>(value));
			} else if (typeId == orb::getTypeId<uint64>()) {
				return static_cast<T>(entt::any_cast<uint64>(value));
			} else if (typeId == orb::getTypeId<bool>()) {
				return static_cast<T>(entt::any_cast<bool>(value));
			}
			
			assert(false);

			return 0;
		}

		template <typename T>
		entt::any numberToAny(T num, TypeId targetType) {
			static_assert(std::is_arithmetic_v<T>);

			if (targetType == orb::getTypeId<f32>()) {
				return entt::any(static_cast<f32>(num));
			} else if (targetType == orb::getTypeId<f64>()) {
				return entt::any(static_cast<f64>(num));
			} else if (targetType == orb::getTypeId<int8>()) {
				return entt::any(static_cast<int8>(num));
			} else if (targetType == orb::getTypeId<int16>()) {
				return entt::any(static_cast<int16>(num));
			} else if (targetType == orb::getTypeId<int32>()) {
				return entt::any(static_cast<int32>(num));
			} else if (targetType == orb::getTypeId<int64>()) {
				return entt::any(static_cast<int64>(num));
			} else if (targetType == orb::getTypeId<uint8>()) {
				return entt::any(static_cast<uint8>(num));
			} else if (targetType == orb::getTypeId<uint16>()) {
				return entt::any(static_cast<uint16>(num));
			} else if (targetType == orb::getTypeId<uint32>()) {
				return entt::any(static_cast<uint32>(num));
			} else if (targetType == orb::getTypeId<uint64>()) {
				return entt::any(static_cast<uint64>(num));
			} else if (targetType == orb::getTypeId<bool>()) {
				return entt::any(static_cast<bool>(num));
			}

			assert(false);

			return 0;
		}

		SliderViewPtr createSlider(std::string_view nameView, const orb::Field& field, entt::any& value, size_t groupId, size_t fieldId) {
			assert(!value.owner());

			std::string name;
			if (const TypedAttribute<DisplayName>* displayName = field.findAttribute<DisplayName>(); displayName) {
				name = displayName->getValue().getName();
			} else {
				name = StringUtil::formatMemberName(nameView);
			}

			SliderViewPtr slider = addProperty<SliderView>(name);

			if (const TypedAttribute<Range>* range = field.findAttribute<Range>(); range) {
				slider->setRange(range->getValue().getMin(), range->getValue().getMax());
			}

			if (const TypedAttribute<StepSize>* stepSize = field.findAttribute<StepSize>(); stepSize) {
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

		DropDownMenuViewPtr createDropDown(const TypeRegistry& reg, std::string_view nameView, const orb::Field& field, entt::any& value, size_t groupId, size_t fieldId) {
			assert(reg.getTypeInfo(field.type).isEnum());

			std::vector<std::string> items;

			const orb::TypeInfo& enumType = reg.getTypeInfo(field.type);

			for (const orb::Field& enumField : enumType.fields) {
				std::string enumFieldName = StringUtil::formatMemberName(enumField.name);
				items.push_back(enumFieldName);
			}

			std::string name;
			if (const TypedAttribute<DisplayName>* displayName = field.findAttribute<DisplayName>(); displayName) {
				name = displayName->getValue().getName();
			} else {
				name = StringUtil::formatMemberName(nameView);
			}

			DropDownMenuViewPtr dropdown = addProperty<DropDownMenuView>(name);

			auto valueType = reg.getTypeInfo(value);

			dropdown->setItems(items);
			dropdown->setValue(0);
			//dropdown->setValue(anyToNumber<int32>(value));

			dropdown->ValueChangeEvent = [groupId, fieldId, this](int32 v) {
				assert(!_fieldGroups[groupId].fields[fieldId].value.owner());

				entt::any val = _fieldGroups[groupId].fields[fieldId].value.as_ref();
				assert(!val.owner());

				bool valid = val.assign(numberToAny(v, orb::getTypeId(val)));
				assert(valid);
				assert(!val.owner());

				assert(!_fieldGroups[groupId].fields[fieldId].value.owner());
			};

			return dropdown;
		}
	};

	using ObjectInspectorViewPtr = std::shared_ptr<ObjectInspectorView>;
}
