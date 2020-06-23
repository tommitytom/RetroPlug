#include "Wrappers.h"

#include <sol/sol.hpp>

#include "model/Project.h"
#include "model/AudioContextProxy.h"
#include "view/ViewWrapper.h"
#include "platform/Path.h"
#include "platform/Shell.h"

void luawrappers::registerRetroPlug(sol::state& s) {
	s.create_named_table("nativeshell",
		"getContentPath", getContentPath,
		"openShellFolder", openShellFolder
	);

	s.new_usertype<SystemDesc>("SystemDesc",
		"new", sol::factories(
			[]() { return std::make_shared<SystemDesc>(); },
			[](const SystemDesc& other) { return std::make_shared<SystemDesc>(other); }
		),
		"idx", &SystemDesc::idx,
		"emulatorType", &SystemDesc::emulatorType,
		"state", &SystemDesc::state,
		"romName", &SystemDesc::romName,
		"romPath", &SystemDesc::romPath,
		"sramPath", &SystemDesc::sramPath,
		"sameBoySettings", &SystemDesc::sameBoySettings,
		"romData", &SystemDesc::romData,
		"sramData", &SystemDesc::sramData,
		"stateData", &SystemDesc::stateData,
		"fastBoot", &SystemDesc::fastBoot,
		"audioComponentState", &SystemDesc::audioComponentState,
		"uiComponentState", &SystemDesc::uiComponentState,
		"area", &SystemDesc::area,
		"buttons", &SystemDesc::buttons,
		"clear", &SystemDesc::clear
	);

	s.new_usertype<AudioContextProxy>("AudioContextProxy",
		"loadRom", &AudioContextProxy::loadRom,
		"getProject", &AudioContextProxy::getProject,
		"clearProject", &AudioContextProxy::clearProject,
		"setSystem", &AudioContextProxy::setSystem,
		"duplicateSystem", &AudioContextProxy::duplicateSystem,
		"removeSystem", &AudioContextProxy::removeSystem,
		"resetSystem", &AudioContextProxy::resetSystem,
		"getFileManager", &AudioContextProxy::getFileManager,
		"updateSettings", &AudioContextProxy::updateSettings,
		"fetchSystemStates", &AudioContextProxy::fetchSystemStates,
		"fetchResources", &AudioContextProxy::fetchResources,
		"fetchResourcesAsync", &AudioContextProxy::fetchResourcesAsync,
		"setRom", &AudioContextProxy::setRom,
		"setSram", &AudioContextProxy::setSram,
		"setState", &AudioContextProxy::setState,
		"updateSram", &AudioContextProxy::updateSram,
		"updateSystemSettings", &AudioContextProxy::updateSystemSettings,
		"onMenu", &AudioContextProxy::onMenu
	);

	s.new_usertype<ViewWrapper>("ViewWrapper",
		"requestDialog", &ViewWrapper::requestDialog,
		"requestMenu", &ViewWrapper::requestMenu
	);

	s.new_usertype<Rect>("Rect",
		sol::constructors<Rect(), Rect(float, float, float, float)>(),
		"x", &Rect::x,
		"y", &Rect::y,
		"w", &Rect::w,
		"h", &Rect::h,
		"right", &Rect::right,
		"bottom", &Rect::bottom,
		"contains", &Rect::contains
	);

	s.new_usertype<Point>("Point",
		sol::constructors<Point(), Point(float, float)>(),
		"x", &Point::x,
		"y", &Point::y
	);

	s.new_usertype<iplug::IKeyPress>("IKeyPress",
		"vk", &iplug::IKeyPress::VK,
		"shift", &iplug::IKeyPress::S,
		"ctrl", &iplug::IKeyPress::C,
		"alt", &iplug::IKeyPress::A
	);

	s.new_usertype<iplug::igraphics::IMouseMod>("IMouseMod",
		"left", &iplug::igraphics::IMouseMod::L,
		"right", &iplug::igraphics::IMouseMod::R,
		"shift", &iplug::igraphics::IMouseMod::S,
		"ctrl", &iplug::igraphics::IMouseMod::C,
		"alt", &iplug::igraphics::IMouseMod::A
	);

	s.new_enum("ResourceType",
		"None", ResourceType::None,
		"Rom", ResourceType::Rom,
		"Sram", ResourceType::Sram,
		"State", ResourceType::State,
		"Components", ResourceType::Components,
		"AllExceptRom", ResourceType::AllExceptRom,
		"All", ResourceType::All
	);

	s.new_usertype<FetchStateRequest>("FetchStateRequest",
		"systems", &FetchStateRequest::systems
	);

	s.new_usertype<FetchStateResponse>("FetchStateResponse",
		"srams", &FetchStateResponse::srams,
		"states", &FetchStateResponse::states,
		"components", &FetchStateResponse::components
	);
}
