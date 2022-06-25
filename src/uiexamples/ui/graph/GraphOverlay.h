#pragma once

#include "ui/View.h"

namespace rp {
	class GraphOverlay : public View {
	public:
		GraphOverlay() { setType<GraphOverlay>(); }
		GraphOverlay(Dimension dimensions) : View(dimensions) { setType<GraphOverlay>(); }
		~GraphOverlay() { }

		void onRender(Canvas& canvas) override {

		}
	};

	using GraphOverlayPtr = std::shared_ptr<GraphOverlay>;
}
