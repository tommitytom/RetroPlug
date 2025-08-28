#include "WebApplicationRunner.h"

#include <spdlog/spdlog.h>

#include <emscripten/emscripten.h>
#include "audio/WebAudioManager.h"

#include "graphics/gl/GlRenderContext.h"
#include "application/GlfwNativeWindow.h"

namespace fw::app {
	static void webFrameCallback(void* arg) {
		WebApplicationRunner* app = reinterpret_cast<WebApplicationRunner*>(arg);
		app->runFrame();
	}

	WebApplicationRunner::~WebApplicationRunner() {
		destroy();
	}

	void WebApplicationRunner::destroy() {
		_audioManager = nullptr;
		_uiContext = nullptr;
	}

	void WebApplicationRunner::setup(EMSCRIPTEN_WEBAUDIO_T audioContextId, const std::string& canvasId) {
		_audioManager = std::make_shared<audio::WebAudioManager>(audioContextId);
		_audioManager->setProcessor(_app->onCreateAudio());
		_audioManager->start(-1);

		auto resourceManager = std::make_shared<ResourceManager>();
		auto fontManager = std::make_shared<fw::FontManager>(resourceManager);
		auto windowManager = std::make_unique<fw::app::GlfwWindowManager>(resourceManager, fontManager);
		auto renderContext = std::make_unique<fw::GlRenderContext>(false);
		_uiContext = std::make_unique<UiContext>(std::move(renderContext), std::move(windowManager));

		ViewPtr view = _app->onCreateUi();
		_window = _uiContext->setup(view, nullptr, canvasId);
		ViewManagerPtr viewManager = _window->getViewManager();
		viewManager->createState<audio::AudioManagerPtr>(_audioManager);
		viewManager->createState<EventNode>(_audioManager->getProcessor()->getEventNode().spawn("Ui"));

		spdlog::info("Web application setup complete {}", (uintptr_t)viewManager->getChild(0).get());
	}

	void WebApplicationRunner::doLoop() {
		emscripten_set_main_loop_arg(&webFrameCallback, this, 0, true);
	}

	bool WebApplicationRunner::runFrame() {
		return _uiContext->runFrame();
	}
}
