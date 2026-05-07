#include <refl.hpp>

#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"

//#include STRINGIFY(CAT_2(APPLICATION_IMPL,.h))
//#include INCLUDE_APPLICATION(APPLICATION_IMPL)
#include "RetroPlugApplication.h"

#if defined(FW_RENDERER_GL)
#include "graphics/gl/GlRenderContext.h"
using RenderContextT = orb::GlRenderContext;
#else
#include "graphics/gl/GlRenderContext.h"
using RenderContextT = orb::GlRenderContext;
#endif


#if defined(FW_PLATFORM_WEB)
#include "audio/WebAudioManager.h"
using AudioManagerT = orb::audio::WebAudioManager;
#elif defined(FW_PLATFORM_PLUGIN)
#include "audio/AudioManager.h"
using AudioManagerT = orb::audio::AudioManager;
#else
#include "audio/MiniAudioManager.h"
using AudioManagerT = orb::audio::MiniAudioManager;
#endif


#ifdef FW_PLATFORM_WEB
#include "app/WebAudio.h"

EM_ASYNC_JS(void, setupWebFs, (), {
	await setupFs();
});
#endif

orb::app::ApplicationRunner runner;

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

int main() {
	runner.setup<APPLICATION_IMPL, RenderContextT, AudioManagerT>();
	return runner.doLoop();

	/*{
		app = new RetroPlugApplication("RetroPlug 0.4.0", 320, 288);

#ifdef RP_WEB
		setupWebFs();
		initWebAudio(&s_workletThreadId, app);
#endif

		Window window(app);
		window.run();
		delete app;
	}

	return 0;*/
}
