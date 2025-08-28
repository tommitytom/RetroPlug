#include "application/GlfwNativeWindow.h"

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"

using namespace emscripten;
using namespace rp;

EMSCRIPTEN_BINDINGS(retroPlug) {
	// Bindings for SystemPaths
	value_object<SystemPaths>("SystemPaths")
		.field("romPath", &SystemPaths::romPath)
		.field("sramPath", &SystemPaths::sramPath)
		.field("statePath", &SystemPaths::statePath)
	;

	value_object<SystemSettings>("SystemSettings")
		.field("includeRom", &SystemSettings::includeRom)
		.field("gameLink", &SystemSettings::gameLink)
		.field("reloadRomOnChange", &SystemSettings::reloadRomOnChange)
	;

	value_object<SystemDesc>("SystemDesc")
		.field("paths", &SystemDesc::paths)
		.field("settings", &SystemDesc::settings)
		//.field("services", &SystemDesc::services)
	;

	class_<System>("System")
		.smart_ptr<std::shared_ptr<System>>("System")
		.function("reset", &System::reset)
	;

	class_<Project>("Project")
		.function("addSystem", select_overload<SystemPtr(SystemType, const SystemDesc&, SystemId)>(&Project::addSystem))
		.function("removeSystem", &Project::removeSystem)
	;
}
