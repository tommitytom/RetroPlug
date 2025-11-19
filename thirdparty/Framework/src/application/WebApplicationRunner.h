#pragma once

#ifdef FW_PLATFORM_WEB

#ifdef _MSC_VER
	#define __attribute__(x)
#endif

#include <emscripten/wasmfs.h>
#include <emscripten/webaudio.h>
#include "application/Application.h"
#include "application/UiContext.h"
#include "audio/WebAudioManager.h"

namespace orb::app {
	class WebApplicationRunner {
	private:
		audio::WebAudioManagerPtr _audioManager;
		std::unique_ptr<UiContext> _uiContext;
		std::unique_ptr<Application> _app;
		WindowPtr _window;
		std::chrono::high_resolution_clock::time_point _lastTime;
		std::atomic<backend_t> _opfsBackend = nullptr;

	public:
		WebApplicationRunner(std::unique_ptr<Application>&& app);
		~WebApplicationRunner();

		void setupFileSystem();

		bool isFileSystemReady() const {
			return _opfsBackend != nullptr;
		}

		void setupAudio(EMSCRIPTEN_WEBAUDIO_T audioContextId, f32 sampleRate);

		void setupGraphics(const std::string& canvasId);

		WindowPtr createNamedView(const std::string& name, const std::string& canvasId);

		void destroyGraphics();

		void destroy();

		void focusCanvas();

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
