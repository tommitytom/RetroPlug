#pragma once

#include "ui/LabelView.h"
#include "ui/PanelView.h"

namespace rp {
	class ThreadWarning : public orb::PanelView {
		FwRegisterObject()
	public:
		ThreadWarning();
		~ThreadWarning() = default;

		void onInitialize() override;

		void setTextScale(f32 scale);
	};

	using ThreadWarningPtr = std::shared_ptr<ThreadWarning>;
}