#pragma once

#include "StartView.h"
#include "SystemView.h"
#include "core/Project.h"
#include "ui/GridItem.h"

namespace rp {
	enum class HighlightMode {
		Alpha,
		Outline
	};

	class GridOverlay final : public fw::View {
		FwRegisterObject();
	private:
		fw::ViewIndex _selected = fw::INVALID_VIEW_INDEX;
		f32 _unselectedAlpha = 0.75f;

		HighlightMode _highlightMode = HighlightMode::Alpha;

		fw::ViewPtr _grid;

		int32 _projectVersion = -1;
		bool _refocus = false;

	public:
		void onInitialize() override;

		bool onMouseButton(fw::MouseButton button, bool down, fw::Point pos) override;

		void onLayoutChanged() override;

		void setSelected(fw::ViewIndex index) {
			_selected = index;
			updateLayout();
		}

		GridItemPtr getSelected() const {
			if (_selected < _grid->getChildren().size()) {
				return _grid->getChild(_selected)->sharedFromThis<GridItem>();
			}

			return nullptr;
		}

		void incrementSelection() {
			_selected++;
			_selected %= _grid->getChildren().size();
			updateLayout();
		}

		void refocus() {
			_refocus = true;
		}

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		void setUnselectedAlpha(f32 alpha) {
			_unselectedAlpha = alpha;
			updateLayout();
		}

		void setGrid(fw::ViewPtr grid) {
			_grid = grid;
			updateLayout();
		}

		void updateLayout();
	};

	using GridOverlayPtr = std::shared_ptr<GridOverlay>;
}
