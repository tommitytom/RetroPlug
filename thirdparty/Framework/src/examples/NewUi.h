#pragma once

#include <yoga/Yoga.h>

#include "ui/View.h"
#include "application/Application.h"

namespace fw {
	class Widget {
	private:
		YGNodeRef _yogaNode;

	public:
		Widget(): _yogaNode(YGNodeNew()) {}
		~Widget() { YGNodeFree(_yogaNode); }

		friend class Container;
	};

	using WidgetPtr = std::shared_ptr<Widget>;

	class Container : public Widget {
	public:
		void addChild(WidgetPtr widget) {
			YGNodeInsertChild(_yogaNode, widget->_yogaNode, 0);
		}
	};

	class Button : public Widget {

	};

	class NewUi : public View {
	private:
		YGNodeRef _root;

	public:
		NewUi() : View({ 1024, 768 }) {
			setType<NewUi>();
			setSizingPolicy(SizingPolicy::FitToParent);
			setFocusPolicy(FocusPolicy::Click);

			_root = YGNodeNew();
			YGNodeStyleSetWidth(_root, 500);
			YGNodeStyleSetHeight(_root, 300);
			YGNodeStyleSetAlignItems(_root, YGAlignCenter);
		}

		~NewUi() = default;

		void onInitialize() override {

		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			return false;
		}

		bool onKey(const KeyEvent& ev) override {
			return false;
		}

		void onUpdate(f32 delta) override {

		}

		void onRender(fw::Canvas& canvas) override {

		}
	};

	using NewUiApplication = fw::app::BasicApplication<NewUi, void>;
}
