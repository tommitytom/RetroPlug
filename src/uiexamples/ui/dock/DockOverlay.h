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

		bool onDragMove(DragContext& ctx, Point position) override {
			if (ctx.attached && ctx.attached->getParent() != _floating.get()) {
				spdlog::info("added to floating");

				ctx.attached->setSizingPolicy(SizingPolicy::None);
				ctx.attached->setDimensions({ 300, 300 });

				_floating->addChild(ctx.attached);
			}

			ctx.attached->bringToFront();
			ctx.attached->setPosition(position);

			return true;
		}

		// This will intercept every drop that happens within the docking area
		bool onDrop(DragContext& ctx, Point position) override {
			return false;
		}

		void onRender(Canvas& canvas) override;
	};

	using DockOverlayPtr = std::shared_ptr<DockOverlay>;
}