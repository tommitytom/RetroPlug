#include "Application.h"

#include <spdlog/spdlog.h>

#include "foundation/FoundationModule.h"
#include "audio/MiniAudioManager.h"

#include "GlfwNativeWindow.h"

using namespace fw;
using namespace fw::engine;
using namespace fw::app;

Application::Application() : _audioManager(std::make_shared<audio::MiniAudioManager>()), _uiContext(std::make_shared<UiContext>(_audioManager, true)) {
	FoundationModule::setup();
	_audioManager->start();
}

Application::~Application() {
	_audioManager = nullptr;
	_uiContext = nullptr;
}

bool Application::runFrame() {
	return _uiContext->runFrame();
}

int Application::doLoop() {
#if RP_WEB
	emscripten_set_main_loop_arg(&webFrameCallback, this, 0, true);
#else
	while (runFrame()) {}
#endif

	return 0;
}

void Application::webFrameCallback(void* arg) {
	Application* app = (Application*)arg;
	app->runFrame();
}
