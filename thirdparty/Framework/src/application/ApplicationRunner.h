#pragma once

#include "application/Application.h"
#include "application/UiContext.h"

namespace fw::app {
	class ApplicationRunner {
	private:
		audio::AudioManagerPtr _audioManager;
		std::unique_ptr<UiContext> _uiContext;
		std::unique_ptr<Application> _app;

	public:
		ApplicationRunner() {}
		~ApplicationRunner();

		template <typename ApplicationT, typename RenderContextT, typename AudioContextT>
		static int run() {
			ApplicationRunner runner;
			runner.setup<ApplicationT, RenderContextT, AudioContextT>();
			return runner.doLoop();
		}

		template <typename RenderContextT, typename AudioContextT>
		WindowPtr setup(std::unique_ptr<Application>&& app) {
			return setup(std::forward<std::unique_ptr<Application>>(app), std::make_unique<RenderContextT>(), std::make_shared<AudioContextT>());
		}

		WindowPtr setup(std::unique_ptr<Application>&& app, std::unique_ptr<RenderContext>&& renderContext, std::shared_ptr<audio::AudioManager> audioManager);

		bool isReady() const {
			return _app != nullptr;
		}

		Application& getApplication() {
			return *_app;
		}

		UiContext& getUiContext() {
			return *_uiContext;
		}

		bool runFrame();

		int doLoop();

		void reload();

		void destroy();

		static void webFrameCallback(void* arg);
	};
}