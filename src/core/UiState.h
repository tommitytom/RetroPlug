#pragma once

#include <vector>
#include <memory>

#include "core/SystemProcessor.h"
#include "ui/ViewManager.h"
#include "ui/GridView.h"
#include "ui/GridOverlay.h"

namespace rp {
	class UiState {
	public:
		//Dimension<f32> scale = Dimension<f32>(2, 2);

		ViewManager viewManager;
		GridViewPtr grid;
		//ViewPtr overlayRoot;
		GridOverlayPtr gridOverlay;

		SystemProcessor processor;
	};
}
