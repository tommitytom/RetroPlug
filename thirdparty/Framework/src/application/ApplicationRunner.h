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

		template <typename T>
		static int run() {
			ApplicationRunner runner;
			runner.setup<T>();
			return runner.doLoop();
		}

		template <typename T>
		WindowPtr setup() {
			return setup(std::make_unique<T>());
		}

		WindowPtr setup(std::unique_ptr<Application>&& app);

		bool runFrame();

		int doLoop();

		static void webFrameCallback(void* arg);
	};
}