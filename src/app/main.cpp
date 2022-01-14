#include "platform/Window.h"
#include "app/RetroPlugApplication.h"

#ifdef RP_WEB
#include "app/WebAudio.h"
#endif

using namespace rp;

RetroPlugApplication* app = nullptr;

int main() {
	{
		app = new RetroPlugApplication("RetroPlug 0.4.0", 320, 288);

#ifdef RP_WEB
		initWebAudio(&s_workletThreadId, app);
#endif

		Window window(app);
		window.run();
		delete app;
	}

	return 0;
}
