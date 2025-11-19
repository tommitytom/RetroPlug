#include "application/GlfwNativeWindow.h"

#ifdef _MSC_VER
	#define __attribute__(x)
#endif

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "application/WebApplicationRunner.h"
#include "entry/ApplicationFactory.h"
#include "ui/View.h"

using namespace emscripten;
using namespace fw;

orb::app::WebApplicationRunner* makeRunner() {
	return new orb::app::WebApplicationRunner(orb::ApplicationFactory::create());
}

val Uint8Buffer_data(Uint8Buffer& buffer) {
	return val(typed_memory_view(buffer.size(), buffer.data()));
}

val Float32Buffer_data(Float32Buffer& buffer) {
	return val(typed_memory_view(buffer.size(), buffer.data()));
}

EMSCRIPTEN_BINDINGS(orb) {
	class_<orb::View>("View")
		.smart_ptr<std::shared_ptr<orb::View>>("ViewPtr")
	;

	class_<orb::app::Window>("Window")
		.smart_ptr<std::shared_ptr<orb::app::Window>>("WindowPtr")
		.function("requestClose", &orb::app::Window::requestClose)
	;

	class_<orb::app::Application>("NativeApplication")
	;

	class_<orb::app::WebApplicationRunner>("WebApplicationRunner")
		.constructor(&makeRunner, allow_raw_pointers())
		.function("setupFileSystem", &orb::app::WebApplicationRunner::setupFileSystem)
		.function("isFileSystemReady", &orb::app::WebApplicationRunner::isFileSystemReady)
		.function("setupAudio", &orb::app::WebApplicationRunner::setupAudio)
		.function("setupGraphics", &orb::app::WebApplicationRunner::setupGraphics)
		.function("destroyGraphics", &orb::app::WebApplicationRunner::destroyGraphics)
		.function("createNamedView", &orb::app::WebApplicationRunner::createNamedView)
		.function("start", &orb::app::WebApplicationRunner::start)
		.function("stop", &orb::app::WebApplicationRunner::stop)
		.function("getView", &orb::app::WebApplicationRunner::getView)
		.function("getApplication", &orb::app::WebApplicationRunner::getApplication, return_value_policy::reference())
		.function("runFrame", &orb::app::WebApplicationRunner::runFrame)
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
