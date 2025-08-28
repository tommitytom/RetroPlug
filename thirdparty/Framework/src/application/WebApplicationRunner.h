#pragma once

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

	public:
		WebApplicationRunner(std::unique_ptr<Application>&& app) : _app(std::move(app)) {}
		~WebApplicationRunner();

		void setup(EMSCRIPTEN_WEBAUDIO_T audioContextId, const std::string& canvasId);

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

		bool runFrame();

		void doLoop();
	};
}