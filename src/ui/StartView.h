#pragma once

#include "ui/SystemContainerView.h"
#include "ui/MenuView.h"

namespace rp {
	class StartView final : public SystemContainerView {
		FwRegisterObject();

	private:
		MenuViewPtr _menu;

	public:
		StartView() {}
		~StartView() {}

		void onInitialize() override;

		bool onDrop(const std::vector<std::string>& paths) override;

		void processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) override;

	private:
		void setupMenu();
	};
}
