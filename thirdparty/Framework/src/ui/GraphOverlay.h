#pragma once

#include "ui/View.h"

namespace fw {
	class GraphOverlay : public View {
		RegisterObject();
	public:
		GraphOverlay() {}
		GraphOverlay(Dimension dimensions) : View(dimensions) {}
		~GraphOverlay() { }

		void onRender(fw::Canvas& canvas) override {

		}
	};

	using GraphOverlayPtr = std::shared_ptr<GraphOverlay>;
}
