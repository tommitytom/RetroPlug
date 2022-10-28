#include "application/Application.h"
#include "RetroPlug.h"

static fw::app::Application* app;

void initMain(int argc, char** argv) {
	app = new fw::app::Application();
	app->setup<rp::RetroPlug>();
}

bool mainLoop() {
	return app->runFrame();
}

void destroyMain() {
	delete app;
}
