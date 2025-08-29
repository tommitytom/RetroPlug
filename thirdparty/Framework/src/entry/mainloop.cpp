#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"
#include "entry/ApplicationFactory.h"
#include "application/GlfwNativeWindow.h"

using namespace fw;

fw::app::ApplicationRunner runner;

#include "graphics/gl/GlRenderContext.h"
using RenderContextT = fw::GlRenderContext;

#if defined(FW_PLATFORM_WEB)
#error "Web platform is not supported"
#endif

#if defined(FW_PLATFORM_PLUGIN)
#include "audio/AudioManager.h"
using AudioManagerT = fw::audio::AudioManager;
#else
#include "audio/MiniAudioManager.h"
using AudioManagerT = fw::audio::MiniAudioManager;
#endif

void initMain(int argc, char** argv) {
	fw::ResourceManagerPtr resourceManager = std::make_shared<ResourceManager>();
	std::shared_ptr<fw::FontManager> fontManager = std::make_shared<fw::FontManager>(resourceManager);
	runner.setup(ApplicationFactory::create(), std::make_unique<fw::app::GlfwWindowManager>(resourceManager, fontManager), std::make_unique<RenderContextT>(), std::make_unique<AudioManagerT>());
}

bool mainLoop() {
	return runner.runFrame();
}

void destroyMain() {
	runner.destroy();
}

void reload() {
	runner.reload();
}

