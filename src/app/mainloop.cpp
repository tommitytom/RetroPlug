#include "application/Application.h"
#include "RetroPlug.h"

static rp::app::Application* app;

void initMain(int argc, char** argv) {
	app = new rp::app::Application();
	app->setup<rp::RetroPlug>();
}

bool mainLoop() {
	return app->runFrame();
}

void destroyMain() {
	delete app;
}
