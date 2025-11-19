#include "ApplicationRunner.h"

#ifdef FW_PLATFORM_WEB
#include <emscripten/emscripten.h>
#include "audio/WebAudioManager.h"
#else
#include "audio/MiniAudioManager.h"
#endif

namespace orb::app {
	using hrc = std::chrono::high_resolution_clock;
	using delta_duration = std::chrono::duration<f32>;

	ApplicationRunner::~ApplicationRunner() {
		destroy();
	}

	void ApplicationRunner::destroy() {
		_audioManager = nullptr;
		_uiContext = nullptr;
	}

	WindowPtr ApplicationRunner::setup(std::unique_ptr<Application>&& app, std::unique_ptr<WindowManager>&& windowManager, std::unique_ptr<RenderContext>&& renderContext, std::shared_ptr<audio::AudioManager> audioManager) {
		_app = std::move(app);

		AudioProcessorPtr audioProcessor = audioManager ? _app->onCreateAudio() : nullptr;
		ViewPtr view = renderContext ? _app->onCreateUi() : nullptr;

		assert(view || audioProcessor);

		if (audioProcessor) {
			_audioManager = std::move(audioManager);			
			_audioManager->setProcessor(audioProcessor);
			_audioManager->start(-1);
		}

		if (view) {
			_uiContext = std::make_unique<UiContext>(std::move(renderContext), std::move(windowManager));
			WindowPtr window = _uiContext->setup(view);
			ViewManagerPtr viewManager = window->getViewManager();

			if (_audioManager) {
				viewManager->createState<audio::AudioManagerPtr>(_audioManager);
				//viewManager->createState<EventNode>(audioProcessor->getEventNode().spawn("Ui"));
			} else {
				//viewManager->createState<EventNode>(EventNode("Ui"));
			}
		}

		//_app->onInitialize(*_uiContext, _audioManager);

		_lastTime = hrc::now();

		return _uiContext->getMainWindow();
	}

	bool ApplicationRunner::runFrame() {
		hrc::time_point time = hrc::now();
		std::chrono::nanoseconds nanoDelta = time - _lastTime;
		f32 delta = std::chrono::duration_cast<delta_duration>(nanoDelta).count();
		_lastTime = time;

		_app->onUpdate(delta);

		if (_uiContext) {
			return _uiContext->runFrame(delta);
		}

		return true;
	}

	void ApplicationRunner::reload() {
		_uiContext->handleHotReload();
	}

	int ApplicationRunner::doLoop() {
#ifdef FW_PLATFORM_WEB
		emscripten_set_main_loop_arg(&webFrameCallback, this, 0, true);
#else
		while (runFrame()) {}
#endif

		return 0;
	}

	void ApplicationRunner::webFrameCallback(void* arg) {
		ApplicationRunner* app = reinterpret_cast<ApplicationRunner*>(arg);
		app->runFrame();
	}
}
