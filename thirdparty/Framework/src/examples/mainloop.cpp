#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"
#include <filesystem>
#include <assert.h>
#include <Windows.h>
#include "LivePP/API/LPP_API_x64_CPP.h"

#ifndef APPLICATION_HEADER
#define APPLICATION_HEADER APPLICATION_IMPL
#endif

//#include INCLUDE_APPLICATION(APPLICATION_HEADER)

//#include "Whitney.h"
//#include "CanvasTest.h"
//#include "Granular.h"
//#include "StaticReflection.h"
#include "NewUi.h"

using namespace fw;

fw::app::ApplicationRunner runner;

#define FW_RENDERER_BGFX

#if defined(FW_RENDERER_GL)
#include "graphics/gl/GlRenderContext.h"x 
using RenderContextT = fw::GlRenderContext;
#elif defined(FW_RENDERER_BGFX)
#include "graphics/bgfx/BgfxRenderContext.h"
using RenderContextT = fw::BgfxRenderContext;
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
	runner.setup<APPLICATION_IMPL, RenderContextT, AudioManagerT>();
}

bool mainLoop() {
	return runner.runFrame();
}

void destroyMain() {
	runner.destroy();
}

void reload(lpp::LppHotReloadPostpatchHookId, const wchar_t* const recompiledModulePath, const wchar_t* const* const modifiedFiles, unsigned int modifiedFilesCount, const wchar_t* const* const modifiedClassLayouts, unsigned int modifiedClassLayoutsCount) {
	runner.reload();
}