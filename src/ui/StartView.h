#pragma once

#include "core/System.h"
#include "lsdj/LsdjCanvasView.h"
#include "ui/SystemView.h"
#include "ui/SamplerView.h"
#include "ui/SynthView.h"
#include "ui/MenuView.h"
#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjOverlay.h"
#include "ui/SystemContainerView.h"

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
