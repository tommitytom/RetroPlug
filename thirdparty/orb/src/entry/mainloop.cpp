#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"
#include "entry/ApplicationFactory.h"
#include "application/GlfwNativeWindow.h"

using namespace orb;

orb::app::ApplicationRunner runner;

#include "graphics/gl/GlRenderContext.h"
using RenderContextT = orb::GlRenderContext;

#if defined(FW_PLATFORM_WEB)
#error "Web platform is not supported"
#endif

#if defined(FW_PLATFORM_PLUGIN)
#include "audio/AudioManager.h"
using AudioManagerT = orb::audio::AudioManager;
#else
#include "audio/MiniAudioManager.h"
#include "midi/RtMidiManager.h"
using AudioManagerT = orb::audio::MiniAudioManager;
#endif

void initMain(int argc, char** argv) {
	orb::ResourceManagerPtr resourceManager = std::make_shared<ResourceManager>();
	std::shared_ptr<orb::FontManager> fontManager = std::make_shared<orb::FontManager>(resourceManager);
	std::shared_ptr<orb::audio::AudioManager> audioManager = std::make_shared<AudioManagerT>();
	
#ifndef FW_PLATFORM_PLUGIN
	audioManager->setMidiManager(std::make_shared<orb::midi::RtMidiManager>());
#endif

	runner.setup(ApplicationFactory::create(), std::make_unique<orb::app::GlfwWindowManager>(resourceManager, fontManager), std::make_unique<RenderContextT>(), audioManager);
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

