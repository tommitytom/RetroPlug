#include "platform/Window.h"
#include "ExampleApplication.h"

using namespace rp;

static ExampleApplication* app;
static Window* window;

void initMain(int argc, char** argv) {
	app = new ExampleApplication("UI Test", 800, 600);
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
