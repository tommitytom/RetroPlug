#include "application/GlfwNativeWindow.h"

#ifdef _MSC_VER
	#define __attribute__(x)
#endif

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

val Float32Buffer_data(Float32Buffer& buffer) {
	return val(typed_memory_view(buffer.size(), buffer.data()));
}

EMSCRIPTEN_BINDINGS(framework) {
	class_<fw::View>("View")
		.smart_ptr<std::shared_ptr<fw::View>>("ViewPtr")
	;

	class_<fw::app::Application>("NativeApplication")
	;

	class_<fw::app::WebApplicationRunner>("WebApplicationRunner")
		.constructor(&makeRunner, allow_raw_pointers())
		.function("setupFileSystem", &fw::app::WebApplicationRunner::setupFileSystem)
		.function("isFileSystemReady", &fw::app::WebApplicationRunner::isFileSystemReady)
		.function("setupAudio", &fw::app::WebApplicationRunner::setupAudio)
		.function("setupGraphics", &fw::app::WebApplicationRunner::setupGraphics)
		.function("destroyGraphics", &fw::app::WebApplicationRunner::destroyGraphics)
		.function("start", &fw::app::WebApplicationRunner::start)
		.function("stop", &fw::app::WebApplicationRunner::stop)
		.function("getView", &fw::app::WebApplicationRunner::getView)
		.function("getApplication", &fw::app::WebApplicationRunner::getApplication, return_value_policy::reference())
	;

	class_<Uint8Buffer>("Uint8Buffer")
		.smart_ptr<std::shared_ptr<Uint8Buffer>>("Uint8BufferPtr")
		.constructor(&std::make_shared<Uint8Buffer>)
		.constructor(&std::make_shared<Uint8Buffer, size_t>)
		.function("data", &Uint8Buffer_data)
		.function("size", &Uint8Buffer::size)
		.function("clone", &Uint8Buffer::clone)
	;

	class_<Float32Buffer>("Float32Buffer")
		.smart_ptr<std::shared_ptr<Float32Buffer>>("Float32BufferPtr")
		.constructor(&std::make_shared<Float32Buffer>)
		.constructor(&std::make_shared<Float32Buffer, size_t>)
		.function("data", &Float32Buffer_data)
		.function("size", &Float32Buffer::size)
	;
}
