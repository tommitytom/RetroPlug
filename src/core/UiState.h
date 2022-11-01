#pragma once

#include <vector>
#include <memory>

#include "core/SystemProcessor.h"

#include "ui/CompactLayoutView.h"

namespace rp {
	class UiState {
	public:
		CompactLayoutViewPtr compactLayout;

		SystemProcessor processor;
	};
}
