#pragma once

#include "ui/GridOverlay.h"
#include "ui/GridView.h"

namespace rp {
	class CompactLayoutView final : public fw::View {
	private:
		fw::GridViewPtr _grid;
		GridOverlayPtr _gridOverlay;

	public:
		CompactLayoutView() { 
			setType<CompactLayoutView>(); 
			setSizingPolicy(fw::SizingPolicy::FitToContent);
		}
		~CompactLayoutView() = default;

		void setGridLayout(fw::GridLayout layout) {
			_grid->setLayoutMode(layout);
		}

		void onInitialize() override {
			_grid = this->addChild<fw::GridView>("Grid");
			_gridOverlay = this->addChild<GridOverlay>("Grid Overlay");
			_gridOverlay->setGrid(_grid);
		}

		bool onKey(const fw::KeyEvent& ev) {
			if (ev.key == VirtualKey::Tab) {
				if (ev.down) {
					_gridOverlay->incrementSelection();
				}

				return true;
			}

			return false;
		}

		fw::GridViewPtr& getGrid() {
			return _grid;
		}

		const fw::GridViewPtr& getGrid() const {
			return _grid;
		}

		GridOverlayPtr& getGridOverlay() {
			return _gridOverlay;
		}

		const GridOverlayPtr& getGridOverlay() const {
			return _gridOverlay;
		}
	};

	using CompactLayoutViewPtr = std::shared_ptr<CompactLayoutView>;
}