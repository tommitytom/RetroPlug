#pragma once

#include <array>
#include <vector>

#include "ui/View.h"

namespace fw {
	enum class DropTargetType {
		Center,
		Top,
		Right,
		Bottom,
		Left,

		None
	};

	class DockPanel;
	using DockPanelPtr = std::shared_ptr<DockPanel>;
	using DropTargetArray = std::array<RectT<int32>, (size_t)DropTargetType::None>;

	class DockPanel : public View {
		RegisterObject();
	private:
		enum class DisplayMode {
			None,
			Single,
			Tab,
			Vertical,
			Horizontal
		};

		bool _dragOver = false;
		DropTargetType _dragOverIdx = DropTargetType::None;
		DisplayMode _displayMode = DisplayMode::None;

		DropTargetArray _dropTargets;

		std::vector<f32> _handleOffsets;
		std::vector<Rect> _handleAreas;
		std::vector<DockPanelPtr> _panels;
		std::vector<Rect> _tabAreas;
		int32 _panelIdx = -1;
		int32 _mouseOverTabIdx = -1;

		Rect _titleArea;
		int32 _titleAreaHeight = 20;
		bool _showHeader = true;
		bool _mouseOverHeader = false;

	public:
		const int32 DROP_TARGET_SIZE = 30;
		const int32 DROP_TARGET_DISTANCE = 60; // Distance from center

		DockPanel() {}

		void onChildAdded(ViewPtr child) override {
			DockPanelPtr panel = child->asShared<DockPanel>();

			_panels.push_back(panel);
			_tabAreas.push_back(Rect());

			if (_panelIdx == -1) {
				_panelIdx = 0;
			}

			updateLayout();
		}

		void onChildRemoved(ViewPtr child) override {
			for (size_t i = 0; i < _panels.size(); ++i) {
				if (_panels[i] == child) {
					_panels.erase(_panels.begin() + i);
					_tabAreas.erase(_tabAreas.begin() + i);

					if (_panelIdx == (int32)i) {
						_panelIdx = std::min(_panelIdx, (int32)_panels.size() - 1);
					}

					updateLayout();

					break;
				}
			}
		}

		const DropTargetArray& getDropTargets() const {
			return _dropTargets;
		}

		void onDragEnter(DragContext& ctx, Point position) override;

		bool onDragMove(DragContext& ctx, Point position) override;

		void onDragLeave(DragContext& ctx) override;

		bool onDrop(DragContext& ctx, Point position) override;

		void onResize(const ResizeEvent& ev) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

		void onRender(fw::Canvas& canvas) override;

		bool onMouseMove(Point pos) override {
			_mouseOverHeader = _titleArea.contains(pos);
			_mouseOverTabIdx = -1;

			for (int32 i = 0; i < (int32)_tabAreas.size(); ++i) {
				if (_tabAreas[i].contains(pos)) {
					_mouseOverTabIdx = i;
				}
			}

			return _mouseOverHeader;
		}

		void setCurrentPanel(int32 panelIdx) {
			if (_panelIdx == panelIdx) {
				return;
			}

			if (_panelIdx != -1) {
				_panels[_panelIdx]->setVisible(false);
			}

			_panelIdx = panelIdx;

			if (_panelIdx != -1) {
				_panels[_panelIdx]->setVisible(true);
			}
		}

		bool onMouseButton(MouseButton button, bool down, Point position) override {
			if (button == MouseButton::Left && down) {
				for (int32 i = 0; i < (int32)_tabAreas.size(); ++i) {
					if (_tabAreas[i].contains(position)) {
						setCurrentPanel(i);
						return true;
					}
				}
			}

			return false;
		}

		void onMouseLeave() override {
			_mouseOverHeader = false;
			_mouseOverTabIdx = -1;
		}

		bool mouseOverHeader() const {
			return _mouseOverHeader;
		}

	private:
		void updateLayout();

		void arrangePanels();

		DropTargetType dropTargetUnderCursor(Point position);

		Rect createHandleArea(int32 pixelOffset);
	};
}
