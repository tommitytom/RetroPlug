#pragma once

#include "ui/GridOverlay.h"
#include "ui/GridView.h"

namespace rp {
	class SystemContainerView : public fw::View {
		FwRegisterObject();
	};

	using SystemContainerViewPtr = std::shared_ptr<SystemContainerView>;

	class CompactLayoutView final : public SystemContainerView {
		FwRegisterObject();
	private:
		fw::GridViewPtr _grid;
		GridOverlayPtr _gridOverlay;

	public:
		CompactLayoutView() = default;
		~CompactLayoutView() = default;

		void onInitialize() override;

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
