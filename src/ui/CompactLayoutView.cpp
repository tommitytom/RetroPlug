#include "CompactLayoutView.h"

namespace rp {
	void CompactLayoutView::onInitialize() {
		getLayout().setOverflow(fw::FlexOverflow::Visible);
		_grid = this->addChild<fw::GridView>("Grid");
		_gridOverlay = this->addChild<GridOverlay>("Grid Overlay");
		_gridOverlay->fitToParent();
		_gridOverlay->setGrid(_grid);

		subscribe<fw::ChildAddedEvent>(_grid, [this](const fw::ChildAddedEvent& ev) {
			_gridOverlay->setSelected((fw::ViewIndex)_grid->getChildren().size() - 1);
			_gridOverlay->refocus();
		});
		subscribe<fw::ChildRemovedEvent>(_grid, [this](const fw::ChildRemovedEvent& ev) {
			_gridOverlay->refocus();
		});
	}
}
