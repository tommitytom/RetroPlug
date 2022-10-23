#pragma once

#include "ui/View.h"

namespace fw {
	template <typename ParentT>
	class Overlay : public View {
	public:
		Overlay() {
			setSizingPolicy(SizingPolicy::FitToParent);
		}

		ParentT* getSuper() {
			return getParent()->asRaw<ParentT>();
		}

		ParentT* getSuper() const {
			return getParent()->asRaw<ParentT>();
		}
	};
}
