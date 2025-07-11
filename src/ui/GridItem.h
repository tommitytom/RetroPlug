#pragma once

#include "ui/View.h"

namespace rp {
	class GridItem : public fw::View {
		FwRegisterObject();
	private:
	public:
		GridItem() {}
		~GridItem() = default;

		virtual void createMenu(fw::Menu& menu) = 0;
	};

	using GridItemPtr = std::shared_ptr<GridItem>;
}
