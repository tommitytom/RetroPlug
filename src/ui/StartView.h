#pragma once

#include "core/System.h"
#include "ui/LsdjCanvasView.h"
#include "ui/SystemView.h"
#include "ui/SamplerView.h"
#include "ui/SynthView.h"
#include "ui/MenuView.h"
#include "lsdj/LsdjUi.h"
#include "ui/LsdjOverlay.h"

namespace rp {
	class StartView final : public MenuView {
	public:
		StartView() {
			setType<StartView>();
			setEscCloses(false);
		}

		~StartView() {}

		bool onDrop(const std::vector<std::string>& paths) override;

		void onInitialize() override { setupMenu(); }

	private:
		void setupMenu();
	};
}
