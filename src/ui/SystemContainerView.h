#pragma once

#include "ui/View.h"

namespace rp {
	class SystemContainerView : public fw::View {
		FwRegisterObject();
	private:
	public:
		virtual void processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) = 0;
	};

	using SystemContainerViewPtr = std::shared_ptr<SystemContainerView>;
}
