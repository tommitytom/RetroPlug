#pragma once

#include "ui/View.h"
#include "foundation/MathUtil.h"
#include "ui/Property.h"

namespace fw {
	class DropDownMenuView : public TypedPropertyEditor<int32> {
		RegisterObject()
	private:
		bool _editable = true;

		int32 _selectedIndex = -1;
		std::vector<std::pair<std::string, entt::any>> _items;

	public:		
		std::function<void(int32)> ValueChangeEvent;

		DropDownMenuView() {
			getLayout().setDimensions(Dimension{ 200, 30 });
			setFocusPolicy(FocusPolicy::Click);
		}
		~DropDownMenuView() = default;

		void setItems(std::vector<std::pair<std::string, entt::any>>&& items) {
			_items = std::move(items);
			setValue(0);
		}

		void setItems(const std::vector<std::pair<std::string, entt::any>>& items) {
			_items = items;
			setValue(0);
		}

		void setItems(const std::vector<std::string>& items) {
			_items.clear();

			for (size_t i = 0; i < items.size(); ++i) {
				_items.push_back(std::pair<std::string, entt::any>(items[i], i));
			}

			setValue(0);
		}

		template <const size_t Count>
		void setItems(const std::array<std::string_view, Count>& items) {
			_items.clear();

			for (size_t i = 0; i < items.size(); ++i) {
				_items.push_back(std::pair<std::string, entt::any>(items[i], i));
			}

			setValue(0);
		}

		void clearItems() {
			_items.clear();
			setValue(-1);
		}

		void setValue(const int32& value) override {
			_selectedIndex = MathUtil::clamp(value, -1, (int32)_items.size() - 1);
		}

		void setValue(int32 value, bool triggerEvent) {
			setValue(value);

			if (triggerEvent && ValueChangeEvent) {
				ValueChangeEvent(getValue());
			}
		}

		int32 getValue() const override {
			return _selectedIndex;
		}

		entt::any getValueAny() {
			assert(_selectedIndex != -1);
			return _items[_selectedIndex].second;
		}

		void onInitialize() override {

		}

		void setEditable(bool editable) {
			_editable = editable;
		}

		bool onMouseButton(MouseButton button, bool down, Point pos) override {
			return true;
		}

		bool onMouseMove(Point pos) override {
			return true;
		}

		bool onKey(VirtualKey key, bool down) override {
			if (_items.size()) {
				if (key == VirtualKey::RightArrow) {
					if (down) {
						setValue(_selectedIndex == (int32)_items.size() - 1 ? 0 : _selectedIndex + 1, true);
					}

					return true;
				}

				if (key == VirtualKey::LeftArrow) {
					if (down) {
						setValue(_selectedIndex == 0 ? (int32)_items.size() - 1 : _selectedIndex - 1, true);
					}

					return true;
				}
			}

			return false;
		}

		void onMouseLeave() override {

		}

		void onRender(fw::Canvas& canvas) override {
			DimensionF dim = getDimensionsF();

			std::string text = _selectedIndex != -1 ? _items[_selectedIndex].first : "";

			canvas
				.fillRect(getDimensions(), hasFocus() ? Color4F(0.3f, 0.3f, 0.3f, 1.0f) : Color4F::darkGrey)
				.setTextAlign(TextAlignFlags::Top | TextAlignFlags::Left)
				.text(0.0f, 0.0f, fmt::format("{} - {}", _selectedIndex, text));
		}
	};

	using DropDownMenuViewPtr = std::shared_ptr<DropDownMenuView>;
}

REFL_AUTO(
	type(fw::DropDownMenuView, bases<fw::View>),
	func(getValue, property("value")), func(setValue, property("value"))
)
