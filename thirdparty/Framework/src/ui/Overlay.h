#pragma once

#include "ui/View.h"

namespace fw {
	template <typename ParentT>
	class Overlay : public View {
	public:
		Overlay() {
			setSizingPolicy(SizingPolicy::FitToParent);
		}

		std::shared_ptr<ParentT> getSuper() {
			return getParent()->asShared<ParentT>();
		}

		const std::shared_ptr<ParentT> getSuper() const {
			return getParent()->asShared<ParentT>();
		}
	};
}
