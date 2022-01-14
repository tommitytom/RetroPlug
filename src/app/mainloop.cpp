#include "platform/Window.h"
#include "app/RetroPlugApplication.h"

using namespace rp;

static RetroPlugApplication* app;
static Window* window;

void initMain(int argc, char** argv) {
	app = new RetroPlugApplication("RetroPlug 0.4.0", 320, 288);
	window = new Window(app);
	app->onInit();
}

bool mainLoop() {
	return window->runFrame();
}

void destroyMain() {
	delete window;
	delete app;
}
