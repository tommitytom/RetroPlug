#pragma once

#include <memory>

#include <nanovg.h>
#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "ui/Colors.h"

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
		std::vector<Rect> _handleAreas;
		std::vector<PanelViewPtr> _panels;

		SplitDirection _direction = SplitDirection::Vertical;

		int32 _handleSize = 10;

		int32 _mouseOverIndex = -1;
		int32 _draggingIndex = -1;
		Point _dragStartPos;

	public:
		DockSplitter() {
			setType<DockSplitter>();
			setFocusPolicy(FocusPolicy::Click);
		}

		~DockSplitter() = default;

		void setSplitDirection(SplitDirection dir) {
			_direction = dir;
			updateLayout();
		}

		SplitDirection getSplitDirection() const {
			return _direction;
		}

		template <typename T>
		std::shared_ptr<T> addItem(std::string_view name, size_t offset) {
			auto item = std::make_shared<T>();
			item->setName(name);
			addItem(item, offset);
			return item;
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

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			bool handled = false;

			updateLayout();

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
				_mouseOverIndex = handleAtPosition(position);
				updateLayout();
				return true;
			}

			return false;
		}

		bool onMouseMove(Point pos) override {
			bool consumed = false;

			if (_draggingIndex == -1) {
				_mouseOverIndex = handleAtPosition(pos);
				consumed = _mouseOverIndex != -1;
			} else {
				Rect& handle = _handleAreas[_draggingIndex];

				Rect prevHandle;
				Rect nextHandle = { getDimensions().w, getDimensions().h, 0, 0 };

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

		void onDragEnter(DragContext& ctx, Point position) override {

		}

		bool onDragMove(DragContext& ctx, Point position) override {
			// find quadrant
			return false;
		}

		void onDragLeave(DragContext& ctx) override {

		}

		bool onDrop(DragContext& ctx, Point position) override {
			return false;
		}

		void onRender() override {
			NVGcontext* vg = getVg();
			auto res = getDimensions();

			drawRect(res, RP_COLOR_BACKGROUND);

			for (size_t i = 0; i < _handleAreas.size(); ++i) {
				drawRect(_handleAreas[i], _mouseOverIndex != (int32)i ? RP_COLOR_FOREGROUND : RP_COLOR_FOREGROUND2);
			}
		}

		void onInitialized() override {
			updateLayout();
		}

	private:
		Rect createHandleArea(int32 pixelOffset) {
			if (_direction == SplitDirection::Vertical) {
				return Rect{ pixelOffset - _handleSize / 2, 0, _handleSize, getDimensions().h };
			} else {
				return Rect{ 0, pixelOffset - _handleSize / 2, getDimensions().w, _handleSize };
			}
		}

		void updateLayout() {
			if (_direction == SplitDirection::Vertical) {
				f32 totalWidth = (f32)getDimensions().w;
				int32 h = getDimensions().h;

				int32 prevHandleEnd = 0;

				for (size_t i = 0; i < _handleOffsets.size(); ++i) {
					int32 pixelOffset = (int32)(_handleOffsets[i] * totalWidth);

					_handleAreas[i] = createHandleArea(pixelOffset);
					_panels[i]->setArea({ prevHandleEnd, 0, _handleAreas[i].x - prevHandleEnd, h });

					prevHandleEnd = _handleAreas[i].right();
				}

				if (_panels.size()) {
					_panels.back()->setArea({ prevHandleEnd, 0, getDimensions().w - prevHandleEnd, h });
				}
			} else {
				f32 totalHeight = (f32)getDimensions().h;
				int32 w = getDimensions().w;

				int32 prevHandleEnd = 0;

				for (size_t i = 0; i < _handleOffsets.size(); ++i) {
					int32 pixelOffset = (int32)(_handleOffsets[i] * totalHeight);

					_handleAreas[i] = createHandleArea(pixelOffset);
					_panels[i]->setArea({ 0, prevHandleEnd, w, _handleAreas[i].y - prevHandleEnd });

					prevHandleEnd = _handleAreas[i].bottom();
				}

				if (_panels.size()) {
					_panels.back()->setArea({ 0, prevHandleEnd, w, getDimensions().h - prevHandleEnd });
				}
			}
		}

		int32 handleAtPosition(Point position) const {
			for (size_t i = 0; i < _handleAreas.size(); ++i) {
				if (_handleAreas[i].contains(position)) {
					return (int32)i;
				}
			}

			return -1;
		}
	};

	using DockSplitterPtr = std::shared_ptr<DockSplitter>;
}
