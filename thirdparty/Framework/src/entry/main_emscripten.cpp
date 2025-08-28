#include "application/GlfwNativeWindow.h"

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"
#include "application/WebApplicationRunner.h"
#include "entry/ApplicationFactory.h"

using namespace emscripten;

fw::app::WebApplicationRunner* makeRunner() {
	return new fw::app::WebApplicationRunner(fw::ApplicationFactory::create());
}

EMSCRIPTEN_BINDINGS(framework) {
	class_<fw::app::WebApplicationRunner>("WebApplicationRunner")
		.constructor(&makeRunner, allow_raw_pointers())
		.function("setup", &fw::app::WebApplicationRunner::setup)
	;

	//function("createRunner",)
}
