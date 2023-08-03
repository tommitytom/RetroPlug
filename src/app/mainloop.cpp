#include "application/Application.h"
#include "RetroPlugApplication.h"

#include <LivePP/API/LPP_API_x64_CPP.h>

static fw::app::Application* app;

void initMain(int argc, char** argv) {
	app = new fw::app::Application();
	app->setup<RetroPlug>();
}

bool mainLoop() {
	return app->runFrame();
}

void destroyMain() {
	delete app;
}

void reload(lpp::LppHotReloadPostpatchHookId, const wchar_t* const recompiledModulePath, const wchar_t* const* const modifiedFiles, unsigned int modifiedFilesCount, const wchar_t* const* const modifiedClassLayouts, unsigned int modifiedClassLayoutsCount) {
	runner.reload();
}
