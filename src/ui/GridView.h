#pragma once

#include "View.h"

namespace rp {
	enum class GridLayout {
		Row,
		Column,
		Grid,
		Auto
	};

	class GridView final : public View {
	private:
		GridLayout _layout = GridLayout::Auto;

	public:
		GridView() {
			setType<GridView>(); 
			setSizingMode(SizingMode::FitToContent);
		}

		void onChildAdded(ViewPtr view) final override {
			updateLayout();
			view->focus();
		}

		void onChildRemoved(ViewPtr view) final override {
			updateLayout();
			
			if (!getFocused() && getChildren().size() > 0) {
				getChildren()[0]->focus();
			}
		}

	private:
		void updateLayout() {
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
			}
		}
	};

	using GridViewPtr = std::shared_ptr<GridView>;
}
