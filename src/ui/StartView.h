#pragma once

#include "core/System.h"
#include "core/SystemOrchestrator.h"
#include "ui/LsdjCanvasView.h"
#include "ui/SystemView.h"
#include "ui/SamplerView.h"
#include "ui/SynthView.h"
#include "ui/MenuView.h"
#include "lsdj/LsdjUi.h"
#include "ui/LsdjOverlay.h"

namespace rp {
	class SystemOrchestrator;

	namespace SystemFactoryUtil {

	}

	class StartView final : public MenuView {
	public:
		StartView() {
			setType<StartView>();
			setEscCloses(false);
			setupMenu();
		}

		StartView(SystemOrchestrator* orchestrator) {
			setType<StartView>();
			setEscCloses(false);
			setupMenu();
		}

		~StartView() {}

		bool onDrop(const std::vector<std::string>& paths);

	private:
		void setupMenu();

		bool handleLoad(const std::vector<std::string>& files);
	};
}
