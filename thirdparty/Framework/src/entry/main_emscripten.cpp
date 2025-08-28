#include "application/GlfwNativeWindow.h"

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"
#include "application/WebApplicationRunner.h"
#include "entry/ApplicationFactory.h"
#include "ui/View.h"

using namespace emscripten;
using namespace fw;

fw::app::WebApplicationRunner* makeRunner() {
	return new fw::app::WebApplicationRunner(fw::ApplicationFactory::create());
}

val Uint8Buffer_data(Uint8Buffer& buffer) {
	return val(typed_memory_view(buffer.size(), buffer.data()));
}

EMSCRIPTEN_BINDINGS(framework) {
	class_<fw::View>("View")
		.smart_ptr<std::shared_ptr<fw::View>>("ViewPtr")
	;

	class_<fw::app::WebApplicationRunner>("WebApplicationRunner")
		.constructor(&makeRunner, allow_raw_pointers())
		.function("setup", &fw::app::WebApplicationRunner::setup)
		.function("doLoop", &fw::app::WebApplicationRunner::doLoop)
		.function("getView", &fw::app::WebApplicationRunner::getView)
	;

	class_<Uint8Buffer>("Uint8Buffer")
		.smart_ptr<std::shared_ptr<Uint8Buffer>>("Uint8BufferPtr")
		.constructor(&std::make_shared<Uint8Buffer>)
		.constructor(&std::make_shared<Uint8Buffer, size_t>)
		.function("data", &Uint8Buffer_data)
	;
}
