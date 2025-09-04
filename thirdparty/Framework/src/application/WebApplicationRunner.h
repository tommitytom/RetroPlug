#pragma once

#ifdef FW_PLATFORM_WEB
#include <emscripten/webaudio.h>
#include "application/Application.h"
#include "application/UiContext.h"
#include "audio/WebAudioManager.h"

namespace fw::app {
	class WebApplicationRunner {
	private:
		audio::WebAudioManagerPtr _audioManager;
		std::unique_ptr<UiContext> _uiContext;
		std::unique_ptr<Application> _app;
		WindowPtr _window;
		std::chrono::high_resolution_clock::time_point _lastTime;

	public:
		WebApplicationRunner(std::unique_ptr<Application>&& app);
		~WebApplicationRunner();

		void setupAudio(EMSCRIPTEN_WEBAUDIO_T audioContextId);

		void setupGraphics(const std::string& canvasId);

		void destroyGraphics();

		void destroy();

		bool isReady() const {
			return _app != nullptr;
		}

		Application& getApplication() {
			return *_app;
		}

		UiContext& getUiContext() {
			return *_uiContext;
		}

		ViewPtr getView() const {
			return _window->getView();
		}

		bool runFrame();

		void start();

		void stop();
	};
}
#endif
