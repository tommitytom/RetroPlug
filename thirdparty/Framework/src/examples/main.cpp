#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"

#ifndef APPLICATION_HEADER
#define APPLICATION_HEADER APPLICATION_IMPL
#endif

//#include INCLUDE_APPLICATION(APPLICATION_HEADER)

#include "Whitney.h"
#include "CanvasTest.h"
#include "Granular.h"

using namespace fw;

fw::app::ApplicationRunner runner;

#ifdef FW_PLATFORM_WEB
extern "C" {
	void resize_window(int32 width, int32 height) {
		spdlog::info("Canvas resized to {}x{}", width, height);

		if (runner.isReady()) {
			runner.getUiContext().getMainWindow()->setDimensions({ width, height });
		}
	}

	void advance_frame() {
		if (runner.isReady()) {
			runner.runFrame();
		}
	}
}
#endif

int main() {
	runner.setup<APPLICATION_IMPL>();
	return runner.doLoop();
}
