#include "foundation/MacroTools.h"

#include "application/Application.h"
#include INCLUDE_EXAMPLE(EXAMPLE_IMPL)

using namespace rp;

static rp::app::Application* gApp = nullptr;

void initMain(int argc, char** argv) {
	gApp = new rp::app::Application();
	gApp->setup<EXAMPLE_IMPL>();
}

bool mainLoop() {
	assert(gApp);
	return gApp->runFrame();
}

void destroyMain() {
	delete gApp;
	gApp = nullptr;
}
