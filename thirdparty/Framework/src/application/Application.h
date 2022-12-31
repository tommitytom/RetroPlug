#pragma once

#include <memory>

#include "application/UiContext.h"

namespace fw::app {
	class Application {
	private:
		audio::AudioManagerPtr _audioManager;
		UiContextPtr _uiContext;

	public:
		Application();
		~Application();

		template <typename ViewT, typename AudioT = void>
		static int run() {
			Application app;
			app.setup<ViewT, AudioT>();
			return app.doLoop();
		}

		template <typename ViewT, typename AudioT = void>
		WindowPtr setup() {
			if constexpr (!std::is_same_v<AudioT, void>) {
				_audioManager->setProcessor(std::make_shared<AudioT>());
			}

			return _uiContext->setup<ViewT>();
		}

		bool runFrame();

		std::shared_ptr<audio::AudioManager> getAudioManager() {
			return _audioManager;
		}

	private:
		int doLoop();

		static void webFrameCallback(void* arg);
	};

	using ApplicationPtr = std::shared_ptr<Application>;
}
