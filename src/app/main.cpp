#include "platform/Window.h"
#include "app/RetroPlugApplication.h"

#ifdef RP_WEB
#include "app/WebAudio.h"

EM_ASYNC_JS(void, setupWebFs, (), {
	await setupFs();
});
#endif

using namespace rp;

RetroPlugApplication* app = nullptr;

int main() {
	{
		app = new RetroPlugApplication("RetroPlug 0.4.0", 320, 288);

#ifdef RP_WEB
		setupWebFs();
		initWebAudio(&s_workletThreadId, app);
#endif

		Window window(app);
		window.run();
		delete app;
	}

	return 0;
}
