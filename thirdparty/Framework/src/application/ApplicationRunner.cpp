#include "ApplicationRunner.h"

#include "audio/MiniAudioManager.h"

namespace fw::app {
	ApplicationRunner::~ApplicationRunner() {
		_audioManager = nullptr;
		_uiContext = nullptr;
	}

	WindowPtr ApplicationRunner::setup(std::unique_ptr<Application>&& app) {
		_app = std::move(app);

		AudioProcessorPtr audioProcessor = _app->onCreateAudio();
		ViewPtr view = _app->onCreateUi();

		assert(view || audioProcessor);

		if (audioProcessor) {
			_audioManager = std::make_shared<audio::MiniAudioManager>();
			_audioManager->setProcessor(audioProcessor);
		}

		if (view) {
			_uiContext = std::make_unique<UiContext>(true);
			WindowPtr window = _uiContext->setup(view);
			ViewManagerPtr viewManager = window->getViewManager();

			if (_audioManager) {
				viewManager->createState<audio::AudioManagerPtr>(_audioManager);
				viewManager->createState<EventNode>(audioProcessor->getEventNode().spawn("Ui"));
			} else {
				viewManager->createState<EventNode>(EventNode("Ui"));
			}
		}

		if (_audioManager) {
			_audioManager->start();
		}
		
		//_app->onInitialize(*_uiContext, _audioManager);

		return _uiContext->getMainWindow();
	}

	bool ApplicationRunner::runFrame() {
		return _uiContext->runFrame();
	}

	int ApplicationRunner::doLoop() {
#if RP_WEB
		emscripten_set_main_loop_arg(&webFrameCallback, this, 0, true);
#else
		while (runFrame()) {}
#endif

		return 0;
	}

	void ApplicationRunner::webFrameCallback(void* arg) {
		ApplicationRunner* app = (ApplicationRunner*)arg;
		app->runFrame();
	}
}
