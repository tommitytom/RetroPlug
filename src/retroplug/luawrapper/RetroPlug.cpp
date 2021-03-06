#include "Wrappers.h"

#include <sol/sol.hpp>

#include "model/Project.h"
#include "model/AudioContextProxy.h"
#include "platform/ViewWrapper.h"
#include "platform/Path.h"
#include "platform/Shell.h"
#include "util/Paths.h"

using ITouchID = uintptr_t;
struct IMouseMod
{
	bool L, R, S, C, A;

	ITouchID touchID = 0;
	float touchRadius = 0.f;

	/** /todo
	 * @param l /todo
	 * @param r /todo
	 * @param s /todo
	 * @param c /todo
	 * @param a /todo
	 * @pararm touch /todo */
	IMouseMod(bool l = false, bool r = false, bool s = false, bool c = false, bool a = false, ITouchID touch = 0)
		: L(l), R(r), S(s), C(c), A(a), touchID(touch)
	{}
};



void luawrappers::registerRetroPlug(sol::state& s) {
	s.create_named_table("nativeshell",
		"getConfigPath", []() { return ws2s(getConfigPath().wstring()); },
		"getContentPath", [](const tstring& path) { return ws2s(getContentPath(path).wstring()); }
		#ifdef WIN32
		,"openShellFolder", openShellFolder
		#endif
	);

	s.new_usertype<SystemDesc>("SystemDesc",
		"new", sol::factories(
			[]() { return std::make_shared<SystemDesc>(); },
			[](const SystemDesc& other) { return std::make_shared<SystemDesc>(other); }
		),
		"idx", &SystemDesc::idx,
		"systemType", &SystemDesc::systemType,
		"state", &SystemDesc::state,
		"romName", &SystemDesc::romName,
		"romPath", &SystemDesc::romPath,
		"sramPath", &SystemDesc::sramPath,
		"sameBoySettings", &SystemDesc::sameBoySettings,
		"romData", &SystemDesc::romData,
		"sramData", &SystemDesc::sramData,
		"stateData", &SystemDesc::stateData,
		"fastBoot", &SystemDesc::fastBoot,
		"keyInputConfig", &SystemDesc::keyInputConfig,
		"padInputConfig", &SystemDesc::padInputConfig,
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
		"addSystem", &AudioContextProxy::addSystem,
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
		"updateSelected", &AudioContextProxy::updateSelected,
		"onMenu", &AudioContextProxy::onMenu
	);

	s.new_usertype<ViewWrapper>("ViewWrapper",
		"requestDialog", &ViewWrapper::requestDialog,
		"requestMenu", &ViewWrapper::requestMenu
	);

	s.new_usertype<math::Rect>("Rect",
		sol::constructors<math::Rect(), math::Rect(float, float, float, float)>(),
		"x", &math::Rect::x,
		"y", &math::Rect::y,
		"w", &math::Rect::w,
		"h", &math::Rect::h,
		"right", &math::Rect::right,
		"bottom", &math::Rect::bottom,
		"contains", &math::Rect::contains
	);

	s.new_usertype<math::Point>("Point",
		sol::constructors<math::Point(), math::Point(float, float)>(),
		"x", &math::Point::x,
		"y", &math::Point::y
	);

	s.new_usertype<IMouseMod>("IMouseMod",
		"left", &IMouseMod::L,
		"right", &IMouseMod::R,
		"shift", &IMouseMod::S,
		"ctrl", &IMouseMod::C,
		"alt", &IMouseMod::A
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
