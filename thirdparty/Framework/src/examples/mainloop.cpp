#include "foundation/MacroTools.h"

#include "application/Application.h"
#include "application/ApplicationRunner.h"
//#include INCLUDE_APPLICATION(EXAMPLE_IMPL)

#include "Whitney.h"
#include "CanvasTest.h"
#include "Granular.h"

using namespace fw;

fw::app::ApplicationRunner runner;

void initMain(int argc, char** argv) {
	runner.setup<APPLICATION_IMPL>();
}

bool mainLoop() {
	return runner.runFrame();
}

void destroyMain() {
	runner.destroy();
}
