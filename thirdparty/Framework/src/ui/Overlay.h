#pragma once

#include "ui/View.h"

namespace fw {
	template <typename ParentT>
	class Overlay : public View {
		RegisterObject();
	public:
		Overlay() {
			getLayout().setDimensions(FlexDimensionValue{
				FlexValue(FlexUnit::Percent, 100.0f),
				FlexValue(FlexUnit::Percent, 100.0f)
			});
		}

		std::shared_ptr<ParentT> getSuper() {
			return getParent()->template asShared<ParentT>();
		}

		const std::shared_ptr<ParentT> getSuper() const {
			return getParent()->template asShared<ParentT>();
		}
	};
}
