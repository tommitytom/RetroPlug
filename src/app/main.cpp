#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"

#include INCLUDE_APPLICATION(APPLICATION_IMPL)

#ifdef RP_WEB
#include "app/WebAudio.h"

EM_ASYNC_JS(void, setupWebFs, (), {
	await setupFs();
});
#endif

int main() {
	return fw::app::ApplicationRunner::run<APPLICATION_IMPL>();

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
