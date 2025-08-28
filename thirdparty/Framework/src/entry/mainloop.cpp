#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"
#include "entry/ApplicationFactory.h"

#include "application/GlfwNativeWindow.h"

using namespace fw;

fw::app::ApplicationRunner runner;

//#define FW_RENDERER_GL

#if defined(FW_RENDERER_GL)
#include "graphics/gl/GlRenderContext.h"x
using RenderContextT = fw::GlRenderContext;
#else
#include "graphics/gl/GlRenderContext.h"
using RenderContextT = fw::GlRenderContext;
#endif


#if defined(FW_PLATFORM_WEB)
#include "audio/WebAudioManager.h"
using AudioManagerT = fw::audio::WebAudioManager;
#elif defined(FW_PLATFORM_PLUGIN)
#include "audio/AudioManager.h"
using AudioManagerT = fw::audio::AudioManager;
#else
#include "audio/MiniAudioManager.h"
using AudioManagerT = fw::audio::MiniAudioManager;
#endif

#ifdef FW_PLATFORM_WEB
extern "C" {
	void resize_window(int32 width, int32 height) {
		spdlog::info("Canvas resized to {}x{}", width, height);

		if (runner.isReady()) {
			runner.getUiContext().getMainWindow()->setDimensions({ width, height });
		}
	}

	void advance_frame() {
		if (runner.isReady()) {
			runner.runFrame();
		}
	}
}
#endif

void initMain(int argc, char** argv) {
	fw::ResourceManagerPtr resourceManager = std::make_shared<ResourceManager>();
	std::shared_ptr<fw::FontManager> fontManager = std::make_shared<fw::FontManager>(resourceManager);
	runner.setup(ApplicationFactory::create(), std::make_unique<fw::app::GlfwWindowManager>(resourceManager, fontManager), std::make_unique<RenderContextT>(), std::make_unique<AudioManagerT>());
}

bool mainLoop() {
#ifdef FW_PLATFORM_WEB
	runner.doLoop();
	return false;
#else
	return runner.runFrame();
#endif
}

void destroyMain() {
	runner.destroy();
}

void reload() {
	runner.reload();
}

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"

using namespace emscripten;
using namespace rp;

EMSCRIPTEN_BINDINGS(retroPlug) {
	// Bindings for SystemPaths
	value_object<SystemPaths>("SystemPaths")
		.field("romPath", &SystemPaths::romPath)
		.field("sramPath", &SystemPaths::sramPath)
		.field("statePath", &SystemPaths::statePath)
	;

	value_object<SystemSettings>("SystemSettings")
		.field("includeRom", &SystemSettings::includeRom)
		.field("gameLink", &SystemSettings::gameLink)
		.field("reloadRomOnChange", &SystemSettings::reloadRomOnChange)
	;

	value_object<SystemDesc>("SystemDesc")
		.field("paths", &SystemDesc::paths)
		.field("settings", &SystemDesc::settings)
		//.field("services", &SystemDesc::services)
	;

	class_<rp::System>("System")
		.smart_ptr<std::shared_ptr<rp::System>>("System")
		.function("reset", &rp::System::reset)
	;

	class_<rp::Project>("Project")
		.function("addSystem", select_overload<rp::SystemPtr(rp::SystemType, const rp::SystemDesc&, rp::SystemId)>(&rp::Project::addSystem))
		.function("removeSystem", &rp::Project::removeSystem)
	;
}
