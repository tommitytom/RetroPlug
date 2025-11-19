#pragma once

#include "ui/View.h"

namespace rp {
	class SystemOverlay : public orb::View {
		FwRegisterObject()
	public:
		SystemOverlay() {
			//fitToParent();
			getLayout().setDimensions(100_pc);
			setFocusPolicy(orb::FocusPolicy::Click);
		}
	};
}
