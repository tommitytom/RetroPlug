#include "application/GlfwNativeWindow.h"

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include "core/Project.h"
#include "foundation/DataBuffer.h"
#include "ui/RetroPlugView.h"
#include "lsdj/Ram.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"

using namespace emscripten;
using namespace rp;

std::shared_ptr<RetroPlugView> upcastView(const fw::ViewPtr& view) {
	return std::static_pointer_cast<RetroPlugView>(view);
}

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

	enum_<SaveStateType>("SaveStateType")
        .value("None", SaveStateType::None)
        .value("Sram", SaveStateType::Sram)
        .value("State", SaveStateType::State)
    ;

	value_object<LoadConfig>("LoadConfig")
		.field("desc", &LoadConfig::desc)
		.field("romBuffer", &LoadConfig::romBuffer)
		.field("sramBuffer", &LoadConfig::sramBuffer)
		.field("stateBuffer", &LoadConfig::stateBuffer)
		.field("stateType", &LoadConfig::stateType)
		.field("reset", &LoadConfig::reset)
	;

	class_<System>("System")
		.smart_ptr<std::shared_ptr<System>>("System")
		.function("reset", &System::reset)
	;

	class_<Project>("Project")
		.function("addSystem", select_overload<SystemPtr(SystemType, const SystemDesc&, SystemId)>(&Project::addSystem))
		.function("loadSystem", select_overload<SystemPtr(SystemType, LoadConfig&&, SystemId)>(&Project::addSystem))
		.function("removeSystem", &Project::removeSystem)
	;

	class_<RetroPlugView, base<fw::View>>("RetroPlugView")
		.smart_ptr<std::shared_ptr<RetroPlugView>>("RetroPlugViewPtr")
		.function("getProject", &RetroPlugView::getProject, return_value_policy::reference())
	;

	class_<MemoryAccessor>("MemoryAccessor")
		.function("write", select_overload<size_t, const fw::Uint8Buffer&>(&MemoryAccessor::write))
	;



	function("upcastView", &upcastView);
}
