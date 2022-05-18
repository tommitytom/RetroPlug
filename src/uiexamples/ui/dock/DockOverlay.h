#pragma once

#include "DockPanel.h"

namespace rp {
	class DockOverlay : public View {
	private:
		ViewPtr _docked;
		ViewPtr _floating;

	public:
		DockOverlay() {}
		~DockOverlay() {}

		void setViews(ViewPtr docked, ViewPtr floating) {
			_docked = docked;
			_floating = floating;
		}

		void onUpdate(f32 delta) override;

		// This will intercept every drop that happens within the docking area
		bool onDrop(DragContext& ctx, Point<uint32> position) override {
			return false;
		}

		void onRender() override;
	};

	using DockOverlayPtr = std::shared_ptr<DockOverlay>;
}
