#pragma once

#include "ui/GridOverlay.h"
#include "ui/GridView.h"
#include "ui/SystemContainerView.h"

namespace rp {
	class CompactLayoutView final : public SystemContainerView {
		FwRegisterObject();
	private:
		fw::GridViewPtr _grid;
		GridOverlayPtr _gridOverlay;
		std::weak_ptr<MenuView> _menu;

	public:
		CompactLayoutView() = default;
		~CompactLayoutView() = default;

		void onInitialize() override;

		void onUpdate(f32 delta) override {
			auto dim = _grid->getDimensionsF();
			getLayout().setMinWidth(fw::FlexValue(dim.w));
			getLayout().setMinHeight(fw::FlexValue(dim.h));
		}

		void processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) override;

		GridItemPtr getSelected() const {
			return _gridOverlay->getSelected();
		}

		void setGridLayout(fw::GridLayout layout) {
			_grid->setLayoutMode(layout);
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
