#pragma once

#include "ui/View.h"

namespace orb {
	class GraphOverlay : public View {
		FwRegisterObject();
	public:
		GraphOverlay() {}
		GraphOverlay(Dimension dimensions) : View(dimensions) {}
		~GraphOverlay() { }

		void onRender(orb::Canvas& canvas) override {

		}
	};

	using GraphOverlayPtr = std::shared_ptr<GraphOverlay>;
}
