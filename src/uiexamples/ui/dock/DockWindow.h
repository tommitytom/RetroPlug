#pragma once 

#include "ui/View.h"

#include "DockPanel.h"

namespace rp {
	class DockWindow : public View {
	private:
		uint32 _titleAreaHeight = 20;
		Rect<uint32> _titleArea;
		Rect<uint32> _panelArea;
		std::string _title;
		DockPanelPtr _panel;
		bool _mouseOverHeader = false;
		bool _dragOver = false;

	public:
		DockWindow() { setType<DockWindow>(); }

		void onInitialized() override {
			setDraggable(true);
			updateLayout();
		}

		/*void onChildAdded(ViewPtr child) override {
			if (_overlay) {
				_overlay->bringToFront();
				_panels.push_back(child);
				_tabAreas.push_back(Rect<uint32>());

				if (_panelIdx == -1) {
					_panelIdx = 0;
				}

				updateLayout();
			}
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
		}*/

		void onResize(uint32 w, uint32 h) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

		void onRender() override {
			drawRect(getDimensions(), nvgRGBA(100, 100, 100, 255));

			if (_showHeader) {
				drawRect(_titleArea, (_mouseOverHeader) ? nvgRGBA(190, 190, 190, 255) : nvgRGBA(150, 150, 150, 255));
			}

			if (_panels.size() < 2) {
				std::string_view contentName = "NO CONTENT";

				if (_panels.size() == 1) {
					if (_panels[0]->getName().size()) {
						contentName = _panels[0]->getName();
					} else {
						contentName = "UNNAMED";
					}
				}

				drawText(0, 0, contentName, nvgRGBA(255, 255, 255, 255));
			} else {
				const f32 tabWidth = 120;
				f32 tabOffset = 0;

				for (int32 i = 0; i < (int32)_panels.size(); ++i) {
					std::string_view tabName;

					if (_panels[i]->getName().size()) {
						tabName = _panels[i]->getName();
					} else {
						tabName = "UNNAMED";
					}

					if (_mouseOverTabIdx == i) {
						drawRect(_tabAreas[i], nvgRGBA(190, 190, 190, 255));
					}

					drawText(tabOffset, 0, tabName, nvgRGBA(255, 255, 255, 255));
					tabOffset += tabWidth;
				}
			}
		}

		

		bool onMouseMove(Point<uint32> pos) override {
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

		bool onMouseButton(MouseButton::Enum button, bool down, Point<uint32> position) override {
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

		void onDragStart() override {
			spdlog::info("onDragStart");
		}

		void onDragFinish(DragContext& ctx) override {
			spdlog::info("onDragFinish");
		}

		void setTitle(const std::string& title) {
			_title = title;
		}

	private:
		void updateLayout() {
			_titleArea.dimensions = { getDimensions().w, _titleAreaHeight };
			_panelArea = { 0, _titleAreaHeight, getDimensions().w, getDimensions().h - _titleAreaHeight };

			_overlay->setArea(_panelArea);

			const uint32 TAB_WIDTH = 120;
			uint32 tabOffset = 0;

			for (int32 i = 0; i < (int32)_panels.size(); ++i) {
				_panels[i]->setArea(_panelArea);
				_panels[i]->setVisible(i == _panelIdx);
				_tabAreas[i] = Rect<uint32>(tabOffset, 0, TAB_WIDTH, _titleAreaHeight);
				tabOffset += TAB_WIDTH;
			}
		}
	};

	using DockWindowPtr = std::shared_ptr<DockWindow>;
}
