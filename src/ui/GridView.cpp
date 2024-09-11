#include "GridView.h"

using namespace fw;

GridView::GridView() {

}

void GridView::onInitialize() {
	fw::ViewLayout& layout = getLayout();
	layout.setFlexDirection(fw::FlexDirection::Row);
	layout.setFlexWrap(fw::FlexWrap::Wrap);
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

	fw::DimensionF dimensions;

	switch (layout) {
	case GridLayout::Row: {
		for (ViewPtr& view : children) {
			fw::DimensionF childDimensions = view->getDimensionsF();
			if (std::isnan(childDimensions.w)) {
				childDimensions.w = 160;
			}

			if (std::isnan(childDimensions.h)) {
				childDimensions.h = 144;
			}

			dimensions.w += childDimensions.w;
			dimensions.h = std::max(childDimensions.h, childDimensions.h);
		}

		break;
	}
	case GridLayout::Column:{
		for (ViewPtr& view : children) {
			fw::DimensionF childDimensions = view->getDimensionsF();
			if (std::isnan(childDimensions.w)) {
				childDimensions.w = 160;
			}

			if (std::isnan(childDimensions.h)) {
				childDimensions.h = 144;
			}

			dimensions.w += std::max(dimensions.w, childDimensions.w);
			dimensions.h = childDimensions.h;
		}

		break;
	}
	case GridLayout::Grid: {
		const uint32 colCount = 2;

		if (children.size() <= 2) {
			dimensions.w = 160.0f * static_cast<f32>(children.size());
			dimensions.h = 144.0f;
		} else {
			dimensions.w = 160.0f * colCount;
			dimensions.h = 144.0f * 2;
		}

		break;
	}
	case GridLayout::Auto:
		break;
	}

	fw::FlexDimensionValue v;
	v.width = static_cast<f32>(dimensions.w);
	v.height = static_cast<f32>(dimensions.h);
	getLayout().setMinDimensions(v);
	getLayout().setDimensions(v);
}
