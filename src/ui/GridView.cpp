#include "GridView.h"

using namespace fw;

GridView::GridView() {
	setType<GridView>();
	setSizingPolicy(SizingPolicy::FitToContent);
}

void GridView::onChildAdded(ViewPtr view) {
	updateLayout();
	view->focus();
}

void GridView::onChildRemoved(ViewPtr view) {
	updateLayout();

	/*if (!getFocused() && getChildren().size() > 0) {
		getChildren()[0]->focus();
	}*/
}

void GridView::updateLayout() {
	std::vector<ViewPtr>& children = getChildren();

	GridLayout layout = _layout;
	if (layout == GridLayout::Auto) {
		if (children.size() < 4) {
			layout = GridLayout::Row;
		} else {
			layout = GridLayout::Grid;
		}
	}

	switch (layout) {
	case GridLayout::Row: {
		uint32 xOffset = 0;

		for (ViewPtr& view : children) {
			view->setPosition(xOffset, 0);
			xOffset += view->getArea().w;
		}

		break;
	}
	case GridLayout::Column:{
		uint32 yOffset = 0;

		for (ViewPtr& view : children) {
			view->setPosition(0, yOffset);
			yOffset += view->getArea().h;
		}

		break;
	}
	case GridLayout::Grid: {
		const uint32 colCount = 2;

		for (uint32 i = 0; i < (uint32)children.size(); ++i) {
			ViewPtr& view = children[i];

			uint32 rowIdx = i / colCount;
			uint32 colIdx = i % colCount;

			// NOTE: This will break if child views have differing sizes!
			uint32 xOffset = colIdx * 160;// view->getDimensions().w;
			uint32 yOffset = rowIdx * 144;// view->getDimensions().h;

			view->setPosition(xOffset, yOffset);
		}

		break;
	}
	case GridLayout::Auto:
		break;
	}
}
