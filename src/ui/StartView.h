#pragma once

#include "core/System.h"
#include "lsdj/LsdjCanvasView.h"
#include "ui/SystemView.h"
#include "ui/SamplerView.h"
#include "ui/SynthView.h"
#include "ui/MenuView.h"
#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjOverlay.h"

namespace rp {
	class StartView final : public MenuView {
		RegisterObject();
	public:
		StartView() {
			setEscCloses(false);
		}

		~StartView() {}

		bool onDrop(const std::vector<std::string>& paths) override;

		void onInitialize() override { 
			MenuView::onInitialize();
			setupMenu(); 
		}

	private:
		void setupMenu();
	};
}
