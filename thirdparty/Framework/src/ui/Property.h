#pragma once

#include <entt/core/any.hpp>
#include "ui/View.h"

namespace fw {
	class PropertyEditorBase : public View {
	private:
		bool _valueChanged = false;

	public:
		virtual void setPropertyValue(const entt::any& value) = 0;

		virtual entt::any getPropertyValue() const = 0;

		template <typename PropT>
		PropT getProperyValueAs() const {
			entt::any value = getPropertyValue();
			assert(value.type() == entt::type_id<PropT>());
			return entt::any_cast<PropT>(value);
		}

		void setValueChanged(bool changed) {
			_valueChanged = true;
		}
	};

	using PropertyEditorBasePtr = std::shared_ptr<PropertyEditorBase>;

	template <typename PropT>
	class TypedPropertyEditor : public PropertyEditorBase {
	public:
		void setPropertyValue(const entt::any& value) override {
			assert(value.type() == entt::type_id<PropT>());
			setValue(entt::any_cast<const PropT&>(value));
		}

		entt::any getPropertyValue() const override {
			return entt::make_any<PropT>(getValue());
		}

		virtual void setValue(const PropT& value) = 0;

		virtual PropT getValue() const = 0;
	};
}
