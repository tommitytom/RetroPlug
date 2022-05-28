#pragma once 

#include <nanovg.h>

#include "ui/View.h"

#include "DockPanel.h"

namespace rp {
	class DockWindow : public View {
	private:
		uint32 _titleAreaHeight = 20;
		RectT<uint32> _titleArea;
		RectT<uint32> _panelArea;
		std::string _title;
		DockPanelPtr _panel;
		
		bool _dragOver = false;

	public:
		DockWindow() { setType<DockWindow>(); }

		void onInitialized() override {
			setDraggable(true);
			updateLayout();
		}

		/*void onChildAdded(ViewPtr child) override {

		}

		void onChildRemoved(ViewPtr child) override {

		}*/

		void onResize(uint32 w, uint32 h) override {
			updateLayout();
		}

		void onLayoutChanged() override {
			updateLayout();
		}

		void onRender() override {
			drawRect(getDimensions(), nvgRGBA(100, 100, 100, 255));
		}

		void onDragStart() override {
			//spdlog::info("onDragStart");
		}

		void onDragFinish(DragContext& ctx) override {
			//spdlog::info("onDragFinish");
		}

		void setTitle(const std::string& title) {
			_title = title;
		}

	private:
		void updateLayout() {

		}
	};

	using DockWindowPtr = std::shared_ptr<DockWindow>;
}
