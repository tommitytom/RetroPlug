#pragma once

#include "ui/View.h"

namespace fw {
	enum class GridLayout {
		Auto,
		Row,
		Column,
		Grid
	};

	class GridView final : public View {
		RegisterObject();
	private:
		GridLayout _layout = GridLayout::Auto;

	public:
		GridView();

		void setLayoutMode(GridLayout layout) {
			if (layout != _layout) {
				_layout = layout;
				updateLayout();
			}
		}

		void onInitialize() override;
		
		void onChildAdded(ViewPtr view) override;

		void onChildRemoved(ViewPtr view) override;

	private:
		void updateLayout();
	};

	using GridViewPtr = std::shared_ptr<GridView>;
}
