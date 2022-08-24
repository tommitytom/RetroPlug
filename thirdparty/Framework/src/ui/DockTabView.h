#pragma once

#include "ui/TabView.h"

namespace fw {
	class DockTabView : public TabView {
	public:
		DockTabView() { setType<DockTabView>(); }
		~DockTabView() = default;

		bool onMouseMove(Point pos) override {
			int32 draggingTab = getDraggingTabIdx();
			if (draggingTab != -1 && draggingTab < getChildren().size()) {
				int32 diff = pos.y - getDragStartPosition().y;

				if (abs(diff) >= 50) {
					auto tabWindow = std::make_shared<DockTabView>();
					tabWindow->setName("TabWindow");
					tabWindow->addChild(getChildren()[draggingTab]);

					beginDrag(tabWindow);
				}
			}

			return TabView::onMouseMove(pos);
		}
	};
}