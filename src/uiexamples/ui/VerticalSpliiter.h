#pragma once

#include <nanovg.h>
#include <spdlog/spdlog.h>

#include "ui/View.h"

namespace rp {
	class PanelView : public View {
	private:
		NVGcolor _color = nvgRGBA(0, 0, 0, 255);

	public:
		PanelView() { 
			setType<PanelView>();
		}

		void setColor(NVGcolor color) {
			_color = color;
		}

		void onRender() override {
			drawRect(getDimensions(), _color);
		}
	};

	using PanelViewPtr = std::shared_ptr<PanelView>;

	class DockWindow;
	class DockPanel;
	class Dock;
	class DockSplitter;

	enum class SplitDirection {
		Vertical,
		Horizontal
	};

	class DockSplitter : public View {
	private:
		std::vector<f32> _handleOffsets;
		std::vector<Rect<uint32>> _handleAreas;
		std::vector<PanelViewPtr> _panels;

		SplitDirection _direction = SplitDirection::Vertical;

		uint32 _handleSize = 20;

		int32 _mouseOverIndex = -1;
		int32 _draggingIndex = -1;
		Point<uint32> _dragStartPos;

	public:
		DockSplitter() {
			setType<DockSplitter>();
		}

		~DockSplitter() = default;

		void setSplitDirection(SplitDirection dir) {
			_direction = dir;
			updateLayout();
		}

		SplitDirection getSplitDirection() const {
			return _direction;
		}

		void addItem(ViewPtr view, size_t offset) {
			if (_panels.size()) {
				_handleAreas.push_back({});

				f32 offset = 0.5f;
				if (_handleOffsets.size()) {
					f32 lastOffset = _handleOffsets.back();
					offset = lastOffset + ((1.0f - _handleOffsets.back()) / 2);
				}

				_handleOffsets.push_back(offset);
			}

			PanelViewPtr panel = addChild<PanelView>(fmt::format("Panel [{}]", view->getName()));
			panel->setColor(nvgRGBA(40, 40, 40, 255));
			panel->addChild(view);

			_panels.push_back(panel);

			updateLayout();
		}

		void onResize(uint32 w, uint32 h) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point<uint32> position) override {
			bool handled = false;

			if (button == MouseButton::Left) {
				if (down) {
					_draggingIndex = handleAtPosition(position);

					if (_draggingIndex != -1) {
						_dragStartPos = position;
						handled = true;
					}
				} else {
					if (_draggingIndex != -1) {
						_draggingIndex = -1;
						handled = true;
					}
				}
			}

			if (handled) {
				updateLayout();
				return true;
			}

			return false;
		}

		bool onMouseMove(Point<uint32> pos) override {
			bool consumed = false;

			if (_draggingIndex == -1) {
				_mouseOverIndex = handleAtPosition(pos);
				consumed = _mouseOverIndex != -1;
			} else {
				Rect<uint32>& handle = _handleAreas[_draggingIndex];

				Rect<uint32> prevHandle;
				Rect<uint32> nextHandle = { getDimensions().w, getDimensions().h, 0, 0 };

				if (nextHandle.x == 0 || nextHandle.y == 0) {
					return false;
				}

				if (_draggingIndex > 0) {
					prevHandle = _handleAreas[(size_t)_draggingIndex - 1];
				}

				if (_draggingIndex < _handleAreas.size() - 1) {
					nextHandle = _handleAreas[(size_t)_draggingIndex + 1];
				}

				if (_direction == SplitDirection::Vertical) {
					handle.x = pos.x - _handleSize / 2;

					if (handle.x < prevHandle.right()) {
						handle.x = prevHandle.right();
					} else if (handle.right() > nextHandle.x) {
						handle.x = nextHandle.x - _handleSize;
					}

					_handleOffsets[_draggingIndex] = (f32)handle.getCenter().x / (f32)getDimensions().w;
				} else {
					handle.y = pos.y - _handleSize / 2;

					if (handle.y < prevHandle.bottom()) {
						handle.y = prevHandle.bottom();
					} else if (handle.bottom() > nextHandle.y) {
						handle.y = nextHandle.y - _handleSize;
					}

					_handleOffsets[_draggingIndex] = (f32)handle.getCenter().y / (f32)getDimensions().h;
				}

				consumed = true;
			}

			if (consumed) {
				updateLayout();
			}

			return consumed;
		}

		bool onDragEnter(DragContext& ctx, Point<uint32> position) override {
			return true;

			// Is this drag valid?
			if (ctx.view && ctx.view->isType<DockWindow>()) {
				return true;
			}

			return false;
		}

		void onDragMove(DragContext& ctx, Point<uint32> position) override {
			// find quadrant
		}

		void onDragLeave(DragContext& ctx) override {

		}

		bool onDrop(DragContext& ctx, Point<uint32> position) override {
			return false;
		}

		void onRender() override {
			NVGcontext* vg = getVg();
			auto res = getDimensions();

			drawRect(res, nvgRGBA(100, 100, 100, 255));

			for (size_t i = 0; i < _handleAreas.size(); ++i) {
				drawRect(_handleAreas[i], _mouseOverIndex != (int32)i ? nvgRGBA(255, 255, 0, 255) : nvgRGBA(0, 255, 255, 255));
			}
		}

		void onInitialized() override {
			updateLayout();
		}

	private:
		Rect<uint32> createHandleArea(uint32 pixelOffset) {
			if (_direction == SplitDirection::Vertical) {
				return Rect<uint32>{ pixelOffset - _handleSize / 2, 0, _handleSize, getDimensions().h };
			} else {
				return Rect<uint32>{ 0, pixelOffset - _handleSize / 2, getDimensions().w, _handleSize };
			}
		}

		void updateLayout() {
			if (_direction == SplitDirection::Vertical) {
				f32 totalWidth = (f32)getDimensions().w;
				uint32 h = getDimensions().h;

				uint32 prevHandleEnd = 0;

				for (size_t i = 0; i < _handleOffsets.size(); ++i) {
					uint32 pixelOffset = (uint32)(_handleOffsets[i] * totalWidth);

					_handleAreas[i] = createHandleArea(pixelOffset);
					_panels[i]->setArea({ prevHandleEnd, 0, _handleAreas[i].x - prevHandleEnd, h });

					prevHandleEnd = _handleAreas[i].right();
				}

				if (_panels.size()) {
					_panels.back()->setArea({ prevHandleEnd, 0, getDimensions().w - prevHandleEnd, h });
				}
			} else {
				f32 totalHeight = (f32)getDimensions().h;
				uint32 w = getDimensions().w;

				uint32 prevHandleEnd = 0;

				for (size_t i = 0; i < _handleOffsets.size(); ++i) {
					uint32 pixelOffset = (uint32)(_handleOffsets[i] * totalHeight);

					_handleAreas[i] = createHandleArea(pixelOffset);
					_panels[i]->setArea({ 0, prevHandleEnd, w, _handleAreas[i].y - prevHandleEnd });

					prevHandleEnd = _handleAreas[i].bottom();
				}

				if (_panels.size()) {
					_panels.back()->setArea({ 0, prevHandleEnd, w, getDimensions().h - prevHandleEnd });
				}
			}
		}

		int32 handleAtPosition(Point<uint32> position) const {
			for (size_t i = 0; i < _handleAreas.size(); ++i) {
				if (_handleAreas[i].contains(position)) {
					return (int32)i;
				}
			}

			return -1;
		}
	};

	using DockSplitterPtr = std::shared_ptr<DockSplitter>;

	class DockOverlay : public View {
	private:
		enum DropTargetType {
			Center,
			Top,
			Right,
			Bottom,
			Left,

			None
		};

		bool _dragOver = false;
		int32 _dragOverIdx = DropTargetType::None;

		std::array<Rect<uint32>, DropTargetType::None> _dropTargets;

	public:
		const uint32 DROP_TARGET_SIZE = 30;
		const uint32 DROP_TARGET_DISTANCE = 60; // Distance from center

		DockOverlay() { setType<DockOverlay>(); }

		bool onDragEnter(DragContext& ctx, Point<uint32> position) override {
			if (!ctx.view || ctx.view->isType<DockWindow>()) {
				_dragOver = true;
				_dragOverIdx = dropTargetUnderCursor(position);
				return true;
			}
			
			return false;
		}

		void onDragMove(DragContext& ctx, Point<uint32> position) override {
			if (!ctx.view || ctx.view->isType<DockWindow>()) {
				_dragOverIdx = dropTargetUnderCursor(position);
			}
		}

		void onDragLeave(DragContext& ctx) override {
			_dragOverIdx = DropTargetType::None;
			_dragOver = false;
		}

		bool onDrop(DragContext& ctx, Point<uint32> position) override {
			if (!ctx.view || !ctx.view->isType<DockWindow>()) {
				return false;
			}

			ViewPtr sourceWindow = ctx.view;
			ViewPtr sourceWindowPanel = sourceWindow->getChild(0);
			assert(sourceWindowPanel->getParent());

			View* targetWindow = getParent();
			assert(targetWindow);

			spdlog::info("Dropped on {}", getName());
			spdlog::info("Target window has {} children", targetWindow->getChildren().size());

			ViewPtr targetWindowPanel;
			if (targetWindow->getChildren().size() == 2) {
				targetWindowPanel = targetWindow->getChild(1);
				assert(targetWindowPanel.get() != this);
			}

			DropTargetType targetType = dropTargetUnderCursor(position);

			switch (targetType) {
			case DropTargetType::Center:
				// Add tab
				break;
			case DropTargetType::Left:
				// Add splitter

				if (targetWindowPanel) {
					// Target already has a panel in place

					if (targetWindowPanel->isType<DockSplitter>()) {
						DockSplitterPtr splitter = targetWindowPanel->asShared<DockSplitter>();

						if (splitter->getSplitDirection() == SplitDirection::Vertical) {

						}
					} else if (targetWindowPanel->isType<DockPanel>()) {
						// Add current panel in to a splitter with the dropped item
						targetWindowPanel->remove();

						assert(targetWindow->getChildren().size() == 1);

						std::shared_ptr<DockSplitter> splitter = targetWindow->addChild<DockSplitter>("Vertical Splitter");
						splitter->addItem(sourceWindowPanel, 0);
						splitter->addItem(targetWindowPanel, 0);

						// Overlay needs to be in front
						splitter->pushToBack();
					}
				} else {
					spdlog::info("No panel was found. Setting directly");

					if (sourceWindowPanel) {
						//sourceWindowPanel->remove();
						targetWindow->addChild(sourceWindowPanel);
					}
				}

				sourceWindow->remove();

				break;
			}

			//spdlog::info("drop catch in {}", targetIdx);
			_dragOverIdx = DropTargetType::None;
			_dragOver = false;

			return true;
		}

		void onResize(uint32 w, uint32 h) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

		void onRender() override {
			if (!_dragOver) {
				return;
			}

			for (size_t i = 0; i < _dropTargets.size(); ++i) {
				const Rect<uint32>& target = _dropTargets[i];
				
				if (target != Rect<uint32>()) {
					NVGcolor color = nvgRGBA(0, 255, 0, 255);

					if ((DropTargetType)i == _dragOverIdx) {
						color = nvgRGBA(0, 0, 255, 255);
					}

					drawRect(target, color);
				}
			}

			NVGcolor highlightColor = nvgRGBA(0, 50, 255, 120);
			uint32 highlightMargin = 5;

			if (_dragOverIdx != DropTargetType::None) {
				Rect<uint32> dim;

				switch (_dragOverIdx) {
				case DropTargetType::Center:
					dim = getDimensions();
					break;
				case DropTargetType::Top:
					dim = { 0, 0, getDimensions().w, getDimensions().h / 2 };
					break;
				case DropTargetType::Right:
					dim = { getDimensions().w / 2, 0, getDimensions().w / 2, getDimensions().h };
					break;
				case DropTargetType::Bottom:
					dim = { 0, getDimensions().h / 2, getDimensions().w, getDimensions().h / 2 };
					break;
				case DropTargetType::Left:
					dim = { 0, 0, getDimensions().w / 2, getDimensions().h };
					break;
				}

				drawRect(dim.shrink(highlightMargin), highlightColor);
			}
		}		

	private:
		void updateLayout() {
			if (getArea().w < DROP_TARGET_DISTANCE * 2 || getArea().h < DROP_TARGET_DISTANCE * 2) {
				return;
			}

			Point<uint32> mid = { getArea().w / 2, getArea().h / 2 };
			uint32 x = mid.x - DROP_TARGET_SIZE;
			uint32 y = mid.y - DROP_TARGET_SIZE;
			uint32 w = DROP_TARGET_SIZE;
			uint32 h = DROP_TARGET_SIZE;

			_dropTargets[0] = { x, y, w, h }; // Center
			_dropTargets[1] = { x, y - DROP_TARGET_DISTANCE, w, h }; // Top
			_dropTargets[2] = { x + DROP_TARGET_DISTANCE, y, w, h }; // Right
			_dropTargets[3] = { x, y + DROP_TARGET_DISTANCE, w, h }; // Bottom
			_dropTargets[4] = { x - DROP_TARGET_DISTANCE, y, w, h }; // Left
		}

		DropTargetType dropTargetUnderCursor(Point<uint32> position) {
			for (size_t i = 0; i < _dropTargets.size(); ++i) {
				if (_dropTargets[i].contains(position)) {
					return (DropTargetType)i;
				}
			}

			return DropTargetType::None;
		}
	};
	using DockOverlayPtr = std::shared_ptr<DockOverlay>;

	class DockPanel : public PanelView {
	private:
		ViewPtr _content;

	public:
		DockPanel() { setType<DockPanel>(); }

		void setDockedContent(ViewPtr content) {
			_content = content;
		}

		bool onMouseMove(Point<uint32> pos) override {
			/*if (!_content) {
				return false;
			}

			std::vector<DockWindowPtr> windows;
			_content->findChildren<DockWindow>(windows, true);

			for (const DockWindowPtr& window : windows) {
				if (window->mouseOverHeader()) {

				}
			}*/

			return false;
		}
	};
	using DockPanelPtr = std::shared_ptr<DockPanel>;

	class DockWindow : public View {
	private:
		uint32 _titleAreaHeight = 20;
		Rect<uint32> _titleArea;
		Rect<uint32> _panelArea;
		std::string _title;
		DockOverlayPtr _overlay;
		bool _mouseOverHeader = false;
		bool _dragOver = false;

	public:
		DockWindow() { setType<DockWindow>(); }

		void onInitialized() override {
			setDraggable(true);

			_overlay = addChild<DockOverlay>("Dock Overlay");
			_overlay->setSizingMode(SizingMode::None);
			updateLayout();
		}

		void onResize(uint32 w, uint32 h) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}		

		void onRender() override {
			drawRect(getDimensions(), nvgRGBA(100, 100, 100, 255));
			drawRect(_titleArea, _mouseOverHeader ? nvgRGBA(190, 190, 190, 255) : nvgRGBA(150, 150, 150, 255));
		}

		bool onMouseMove(Point<uint32> pos) override {
			_mouseOverHeader = _titleArea.contains(pos);
			return _mouseOverHeader;
		}

		void onMouseLeave() override {
			_mouseOverHeader = false;
		}

		bool mouseOverHeader() const {
			return _mouseOverHeader;
		}

		void onDragStart() override {
			spdlog::info("onDragStart");
		}

		void onDragFinish(DragContext& ctx) override {
			spdlog::info("onDragFinish");
		}

		void setTitle(const std::string& title) {
			_title = title;
		}

		ViewPtr getPanelContent() {
			if (getChildren().size() == 2) {
				return getChild(0);
			}

			return nullptr;
		}

		void setPanelContent(ViewPtr content) {
			ViewPtr existing = getPanelContent();
			if (existing) {
				existing->remove();
			}

			if (content) {
				content->setSizingMode(SizingMode::None);
				addChild(content);
				_overlay->bringToFront();
			}

			updateLayout();
		}

	private:
		void updateLayout() {
			_titleArea.dimensions = { getDimensions().w, _titleAreaHeight };
			_panelArea = { 0, _titleAreaHeight, getDimensions().w, getDimensions().h - _titleAreaHeight };

			_overlay->setArea(_panelArea);

			ViewPtr content = getPanelContent();
			if (content) {
				content->setArea(_panelArea);
			}
		}
	};

	using DockWindowPtr = std::shared_ptr<DockWindow>;

	class Dock : public View {
	private:
		ViewPtr _dockedRoot;
		ViewPtr _floatingWinows;

	public:
		Dock() { setType<Dock>(); }
		~Dock() = default;

		void onInitialized() override {
			_dockedRoot = addChild<View>("Docked Windows");
			_dockedRoot->setSizingMode(SizingMode::FitToParent);

			_floatingWinows = addChild<View>("Floating Dock Windows");
			_floatingWinows->setSizingMode(SizingMode::FitToParent);
		}

		void setRoot(ViewPtr root) {
			if (_dockedRoot != root) {
				removeChild(_dockedRoot);
			}

			addChild(root);
			_dockedRoot = root;

			root->setSizingMode(SizingMode::FitToParent);
			root->setDimensions(getDimensions());
		}
	};

	using DockPtr = std::shared_ptr<Dock>;	
}
