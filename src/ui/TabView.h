#pragma once

#include "ui/View.h"

namespace rp {
	class TabView : public View {
	private:
		std::vector<ViewPtr> _panels;
		std::vector<Rect> _tabAreas;
		int32 _panelIdx = -1;
		int32 _mouseOverTabIdx = -1;
		int32 _draggingTabIdx = -1;

		Point _dragStartPosition;

		Rect _panelArea;
		Rect _titleArea;
		int32 _titleAreaHeight = 20;
		bool _showHeader = true;
		bool _mouseOverHeader = false;

	public:
		TabView() { setType<TabView>(); setFocusPolicy(FocusPolicy::Click); }
		~TabView() = default;

		void setCurrentPanel(int32 panelIdx);

		void onChildAdded(ViewPtr child) override;

		void onChildRemoved(ViewPtr child) override;

		bool onMouseMove(Point pos) override;

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override;

		void onMouseLeave() override {
			_mouseOverHeader = false;
			_mouseOverTabIdx = -1;
		}

		bool mouseOverHeader() const {
			return _mouseOverHeader;
		}

		int32 getDraggingTabIdx() const {
			return _draggingTabIdx;
		}

		Point getDragStartPosition() const {
			return _dragStartPosition;
		}

		void onRender(Canvas& canvas);

		void onResize(Dimension dimensions) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

	private:
		void updateLayout();
	};
}
