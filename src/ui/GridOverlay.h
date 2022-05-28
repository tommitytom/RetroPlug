#pragma once

#include "StartView.h"
#include "SystemView.h"
#include "core/Project.h"

namespace rp {
	enum class HighlightMode {
		Alpha,
		Outline
	};

	class GridOverlay final : public View {
	private:
		ViewIndex _selected = INVALID_VIEW_INDEX;
		f32 _unselectedAlpha = 0.75f;

		HighlightMode _highlightMode = HighlightMode::Alpha;

		ViewPtr _grid;

		int32 _projectVersion = -1;
		bool _refocus = false;

	public:
		GridOverlay() {
			setType<GridOverlay>(); 
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override;

		void onLayoutChanged() override;

		void setSelected(ViewIndex index) {
			_selected = index;
			updateLayout();
		}

		void incrementSelection() {
			_selected++;
			_selected %= _grid->getChildren().size();
			updateLayout();
		}

		void onUpdate(f32 delta) override;

		void onRender() override;

		void setUnselectedAlpha(f32 alpha) {
			_unselectedAlpha = alpha;
			updateLayout();
		}

		void setGrid(ViewPtr grid) {
			_grid = grid;
			updateLayout();
		}

	private:
		void updateLayout();
	};

	using GridOverlayPtr = std::shared_ptr<GridOverlay>;
}
