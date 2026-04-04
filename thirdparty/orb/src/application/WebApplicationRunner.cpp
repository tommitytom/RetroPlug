#include "WebApplicationRunner.h"

#ifdef FW_PLATFORM_WEB
#include <emscripten/emscripten.h>

#include <thread>
#include <spdlog/spdlog.h>

#include "audio/WebAudioManager.h"
#include "graphics/gl/GlRenderContext.h"
#include "application/GlfwNativeWindow.h"
#include "foundation/FsUtil.h"

namespace orb::app {
	using hrc = std::chrono::high_resolution_clock;
	using delta_duration = std::chrono::duration<f32>;

	static void webFrameCallback(void* arg) {
		WebApplicationRunner* runner = reinterpret_cast<WebApplicationRunner*>(arg);
		runner->runFrame();
	}

	WebApplicationRunner::WebApplicationRunner(std::unique_ptr<Application>&& app) : _app(std::move(app)) {
		auto resourceManager = std::make_shared<ResourceManager>();
		auto fontManager = std::make_shared<orb::FontManager>(resourceManager);
		auto windowManager = std::make_unique<orb::app::GlfwWindowManager>(resourceManager, fontManager);
		auto renderContext = std::make_unique<orb::GlRenderContext>(false);
		_uiContext = std::make_unique<UiContext>(std::move(renderContext), std::move(windowManager));
		_lastTime = hrc::now();
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

	void WebApplicationRunner::setupFileSystem() {
		std::thread([this]() {
			_opfsBackend = wasmfs_create_opfs_backend();
			if (wasmfs_create_directory("/mount", 0777, _opfsBackend) < 0) {
				spdlog::error("Failed to mount root directory");
			} else {
				spdlog::info("Mounted root directory");
			}
		}).detach();
	}

	void WebApplicationRunner::setupAudio(EMSCRIPTEN_WEBAUDIO_T audioContextId, f32 sampleRate) {
		_audioManager = std::make_shared<audio::WebAudioManager>(audioContextId, sampleRate);
		_audioManager->setProcessor(_app->onCreateAudio());
		_audioManager->start(-1);
	}

	void WebApplicationRunner::setupGraphics(const std::string& canvasId) {
		assert(_audioManager);
		ViewPtr view = _app->onCreateUi();
		_window = _uiContext->setup(view, nullptr, canvasId);
		ViewManagerPtr viewManager = _window->getViewManager();
		viewManager->createState<audio::AudioManagerPtr>(_audioManager);
		//viewManager->createState<EventNode>(_audioManager->getProcessor()->getEventNode().spawn("Ui"));
	}

	WindowPtr WebApplicationRunner::createNamedView(const std::string& name, const std::string& canvasId) {
		ViewPtr view = _app->onCreateNamedView(name);
		if (view) {
			WindowPtr window = _uiContext->createWindow(view, nullptr, canvasId);
			ViewManagerPtr viewManager = window->getViewManager();
			viewManager->createState<audio::AudioManagerPtr>(_audioManager);
			return window;
		} else {
			spdlog::error("Failed to create named view: {}", name);
		}

		return nullptr;
	}

	void WebApplicationRunner::destroyGraphics() {
		//_window = nullptr;
	}

	void WebApplicationRunner::focusCanvas() {
		assert(_window);
		_window->focus();
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
		const hrc::time_point time = hrc::now();
		const std::chrono::nanoseconds nanoDelta = time - _lastTime;
		const f32 delta = std::chrono::duration_cast<delta_duration>(nanoDelta).count();
		_lastTime = time;

		_app->onUpdate(delta);

		return _uiContext->runFrame(delta);
	}
}
#endif
