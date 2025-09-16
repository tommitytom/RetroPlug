#include "TileView.h"

#include "TileGrid.h"

namespace rp {
	void TileView::requestSelected() {
		auto tileGrid = findParent<TileGrid>();
		if (tileGrid) {
			tileGrid->requestSelection(_entity);
		}
	}
}
