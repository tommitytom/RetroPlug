#pragma once

#include <entt/core/enum.hpp>

#include "ui/View.h"
#include "ui/next/Stylesheet.h"

namespace fw {
	class ReactView;

	struct ReactStyle {
		StylesheetRule& style;

		void clear() {
			style.properties.clear();
		}

		template <typename T>
		void addProperty(const decltype(T::value)& value) {
			style.setProperty(T{ value });
		}
	};

	enum class InputStateFlag {
		None,
		Hover = 1 << 0,
		Active = 1 << 1,
		_entt_enum_as_bitmask
	};

	struct IdCounter {
		uint32 count = 0;
	};

	class ReactElementView : public View {
		RegisterObject()

	private:
		std::string _id;
		std::string _elementName;
		std::string _className;
		std::vector<std::shared_ptr<StylesheetRule>> _styles;
		bool _styleDirty = false;
		InputStateFlag _inputState = InputStateFlag::None;
		uint32 _tabIndex = -1;

		uint32 _counterId = -1;

	public:
		ReactElementView();
		ReactElementView(const std::string& tag);
		~ReactElementView() {}

		void onInitialize() override {
			updateStyles();
		}

		void setTabIndex(uint32 tabIndex) {
			_tabIndex = tabIndex;
			if (tabIndex != -1) {
				this->setFocusPolicy(FocusPolicy::Click);
			}
		}

		uint32 getTabIndex() const {
			return _tabIndex;
		}

		void setCounterId(uint32 counterId) {
			_counterId = counterId;
		}
		
		uint32 getCounterId() const {
			assert(_counterId != -1);
			return _counterId;
		}

		InputStateFlag getStateFlag() const {
			return _inputState;
		}
		
		void onMouseEnter(Point pos) override;

		void onMouseLeave() override;

		bool onMouseButton(const MouseButtonEvent& ev) override;

		void onRender(fw::Canvas& canvas) override;

		void updateStyles();

		void updateLayoutStyle();

		void onUpdate(f32 dt) override;

		ReactStyle getInlineStyle() {
			_styleDirty = true;
			return ReactStyle{ *_styles[0] };
		}

		const std::string& getId() const {
			return _id;
		}

		void setId(const std::string& id) {
			_id = id;
			updateStyles();
		}

		const std::string& getElementName() const {
			return _elementName;
		}

		void setElementName(const std::string& elementName) {
			_elementName = elementName;
			updateStyles();
		}

		const std::string& getClassName() const {
			return _className;
		}

		void setClassName(const std::string& className) {
			_className = className;
			updateStyles();
		}

		template <typename T>
		const T* inheritProperty() const {
			ViewPtr parent = getParent();
			if (parent && !parent->isType<ReactView>()) {
				return parent->asRaw<ReactElementView>()->template findStyleProperty<T>();
			}

			return nullptr;
		}

		template <typename T>
		const T* findStyleProperty() const {
			for (auto& style : _styles) {
				const T* prop = style->findProperty<T>();
				if (prop) {
					// TODO: Handle revert and revert-layer
					assert(prop->keyword != CssKeyword::Revert && prop->keyword != CssKeyword::RevertLayer);

					switch (prop->keyword) {
						case CssKeyword::None: {
							return prop;
						}
						case CssKeyword::Initial: {
							return &T::Initial;
						}
						case CssKeyword::Inherit: {
							return inheritProperty<T>();
						}
						case CssKeyword::Unset: {
							if constexpr (entt::type_list_contains_v<typename T::Tags, InheritedTag>) {
								return inheritProperty<T>();
							}

							return &T::Initial;
						}
					}
				}
			}

			if constexpr (entt::type_list_contains_v<typename T::Tags, InheritedTag>) {
				return inheritProperty<T>();
			}

			return nullptr;
		}

		template <typename T>
		void findStyleProperty(decltype(T::value)& target) const {
			const T* prop = findStyleProperty<T>();
			if (prop) {
				target = prop->value;
			}
		}

	private:
		template <typename T>
		void setLayoutStyleProperty() {
			const T* prop = findStyleProperty<T>();
			if (prop) {
				getLayout().setProperty<T>(*prop);
			}
		}
	};

	using ReactElementViewPtr = std::shared_ptr<ReactElementView>;
}

REFL_AUTO(
	type(fw::ReactElementView, bases<fw::View>),
	func(getId, property("id")), func(setId, property("id")),
	func(getElementName, property("elementName")), func(setElementName, property("elementName")),
	func(getClassName, property("className")), func(setClassName, property("className"))
)
