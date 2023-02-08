#pragma once

#include "ui/View.h"

namespace rp {
	class SystemOverlay : public fw::View {
	public:
		SystemOverlay() {
			setSizingPolicy(fw::SizingPolicy::FitToParent);
		}
	};
}
