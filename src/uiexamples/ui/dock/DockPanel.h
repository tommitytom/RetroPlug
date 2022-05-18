#pragma once 

#include <array>
#include <vector>

#include "ui/View.h"

namespace rp {
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
	using DropTargetArray = std::array<Rect<uint32>, (size_t)DropTargetType::None>;

	class DockPanel : public View {
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
		std::vector<Rect<uint32>> _handleAreas;
		std::vector<DockPanelPtr> _panels;
		std::vector<Rect<uint32>> _tabAreas;
		int32 _panelIdx = -1;
		int32 _mouseOverTabIdx = -1;

		bool _showHeader = true;

	public:
		const uint32 DROP_TARGET_SIZE = 30;
		const uint32 DROP_TARGET_DISTANCE = 60; // Distance from center

		DockPanel() { setType<DockPanel>(); }

		void onChildAdded(ViewPtr child) override {
			DockPanelPtr panel = child->asShared<DockPanel>();

			_panels.push_back(panel);
			_tabAreas.push_back(Rect<uint32>());

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

		void onDragEnter(DragContext& ctx, Point<uint32> position) override;

		bool onDragMove(DragContext& ctx, Point<uint32> position) override;

		void onDragLeave(DragContext& ctx) override;

		bool onDrop(DragContext& ctx, Point<uint32> position) override;

		void onResize(uint32 w, uint32 h) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

		void onRender() override;

	private:
		void updateLayout();

		void arrangePanels();

		DropTargetType dropTargetUnderCursor(Point<uint32> position);

		Rect<uint32> createHandleArea(uint32 pixelOffset);
	};
}
