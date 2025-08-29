#include "WebApplicationRunner.h"

#ifdef FW_PLATFORM_WEB
#include <emscripten/emscripten.h>

#include <spdlog/spdlog.h>

#include "audio/WebAudioManager.h"
#include "graphics/gl/GlRenderContext.h"
#include "application/GlfwNativeWindow.h"

namespace fw::app {
	static void webFrameCallback(void* arg) {
		WebApplicationRunner* runner = reinterpret_cast<WebApplicationRunner*>(arg);
		runner->runFrame();
	}

	WebApplicationRunner::WebApplicationRunner(std::unique_ptr<Application>&& app) : _app(std::move(app)) {
		auto resourceManager = std::make_shared<ResourceManager>();
		auto fontManager = std::make_shared<fw::FontManager>(resourceManager);
		auto windowManager = std::make_unique<fw::app::GlfwWindowManager>(resourceManager, fontManager);
		auto renderContext = std::make_unique<fw::GlRenderContext>(false);
		_uiContext = std::make_unique<UiContext>(std::move(renderContext), std::move(windowManager));
	}

	WebApplicationRunner::~WebApplicationRunner() {
		destroy();
	}

	void WebApplicationRunner::destroy() {
		_window = nullptr;
		_audioManager = nullptr;
		_uiContext = nullptr;
		_app = nullptr;
	}

	void WebApplicationRunner::setupAudio(EMSCRIPTEN_WEBAUDIO_T audioContextId) {
		_audioManager = std::make_shared<audio::WebAudioManager>(audioContextId);
		_audioManager->setProcessor(_app->onCreateAudio());
		_audioManager->start(-1);
	}

	void WebApplicationRunner::setupGraphics(const std::string& canvasId) {
		assert(_audioManager);
		ViewPtr view = _app->onCreateUi();
		_window = _uiContext->setup(view, nullptr, canvasId);
		ViewManagerPtr viewManager = _window->getViewManager();
		viewManager->createState<audio::AudioManagerPtr>(_audioManager);
		viewManager->createState<EventNode>(_audioManager->getProcessor()->getEventNode().spawn("Ui"));
	}

	void WebApplicationRunner::destroyGraphics() {
		//_window = nullptr;
	}

	void WebApplicationRunner::start() {
		assert(_window);
		emscripten_set_main_loop_arg(&webFrameCallback, this, 0, true);
	}

	void WebApplicationRunner::stop() {
		assert(_window);
		emscripten_cancel_main_loop();
	}

	bool WebApplicationRunner::runFrame() {
		return _uiContext->runFrame();
	}
}
#endif
