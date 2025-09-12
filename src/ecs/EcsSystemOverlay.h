#pragma once

#include "ui/View.h"

namespace rp {
	class EcsSystemOverlay : public fw::View {
		FwRegisterObject()
	public:
		EcsSystemOverlay() {
			//fitToParent();
			getLayout().setDimensions(100_pc);
			setFocusPolicy(fw::FocusPolicy::Click);
		}
	};
}
