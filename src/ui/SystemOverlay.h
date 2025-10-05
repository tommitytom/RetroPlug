#pragma once

#include "ui/View.h"

namespace rp {
	class SystemOverlay : public fw::View {
		FwRegisterObject()
	public:
		SystemOverlay() {
			//fitToParent();
			getLayout().setDimensions(100_pc);
			setFocusPolicy(fw::FocusPolicy::Click);
		}
	};
}
