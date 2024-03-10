#include "GridView.h"

using namespace fw;

GridView::GridView() {

}

void GridView::onInitialize() {
	//getLayout().setDimensions(100_pc);
	//getLayout().setOverflow(fw::FlexOverflow::Visible);
	getLayout().setFlexDirection(fw::FlexDirection::Row);
}

void GridView::onUpdate(f32 dt) {
	//updateLayout();
}

void GridView::onChildAdded(ViewPtr view) {
	updateLayout();
	view->focus();
}

void GridView::onChildRemoved(ViewPtr view) {
	updateLayout();

	if (!getFocused() && getChildren().size() > 0) {
		getChildren()[0]->focus();
	}
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

	fw::Dimension dimensions;

	switch (layout) {
	case GridLayout::Row: {
		for (ViewPtr& view : children) {
			dimensions.w += view->getArea().w;
			dimensions.h = std::max(dimensions.h, view->getArea().h);
		}

		break;
	}
	case GridLayout::Column:{
		for (ViewPtr& view : children) {
			dimensions.w += std::max(dimensions.w, view->getArea().w); 
			dimensions.h = view->getArea().h;
		}

		break;
	}
	case GridLayout::Grid: {
		const uint32 colCount = 2;

		if (children.size() <= 2) {
			dimensions.w = 160 * children.size();
			dimensions.h = 144;
		} else {
			dimensions.w = 160 * colCount;
			dimensions.h = 144 * 2;
		}

		break;
	}
	case GridLayout::Auto:
		break;
	}

	fw::FlexDimensionValue v;
	v.width = dimensions.w;
	v.height = dimensions.h;
	getLayout().setMinDimensions(v);
}
